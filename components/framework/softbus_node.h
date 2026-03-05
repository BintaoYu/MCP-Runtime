#pragma once

#include <iostream>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <chrono>
#include <functional>
#include <string_view>
#include <atomic>
#include <vector> // 引入 vector

#if defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h> 
#endif

#include "common/shm_layout.h"

namespace shm_bus {

inline void nt_memcpy(void* dst, const void* src, size_t size) {
#if defined(__x86_64__) || defined(_M_X64)
    size_t i = 0;
    uint64_t* d_64 = static_cast<uint64_t*>(dst);
    const uint64_t* s_64 = static_cast<const uint64_t*>(src);
    for (; i + 8 <= size; i += 8) _mm_stream_si64(reinterpret_cast<long long*>(d_64++), *s_64++);
    char* d_c = reinterpret_cast<char*>(d_64);
    const char* s_c = reinterpret_cast<const char*>(s_64);
    for (; i < size; ++i) *d_c++ = *s_c++;
    _mm_sfence(); 
#else
    std::memcpy(dst, src, size);
#endif
}

class SoftBusNode {
protected:
    uint32_t my_id_;                 
    void* base_addr_;                
    ShmHeader* header_;              
    ThreadLocalCache local_cache_;   

    int cached_mcp_id_ = -1;
    uint32_t mcp_lookup_counter_ = 0;
    std::atomic<bool> stop_flag_{false};

public:
    SoftBusNode(std::string_view my_name, uint32_t expected_type = 0) 
        : base_addr_(MAP_FAILED), header_(nullptr), local_cache_(nullptr),
          cached_mcp_id_(-1), mcp_lookup_counter_(0) {
        int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
        if (shm_fd < 0) throw std::runtime_error("接入总线失败");
        base_addr_ = mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        close(shm_fd);
        header_ = static_cast<ShmHeader*>(base_addr_);
        new (&local_cache_) ThreadLocalCache(base_addr_);

        bool registered = false;
        for (uint32_t i = 0; i < MAX_NODES; ++i) {
            bool expected = false;
            if (header_->node_registered[i].compare_exchange_strong(expected, true)) {
                my_id_ = i;
                std::size_t copy_len = std::min(my_name.size(), (std::size_t)31);
                std::memcpy(header_->node_names[my_id_], my_name.data(), copy_len);
                header_->node_names[my_id_][copy_len] = '\0';
                header_->expected_msg_type[my_id_].store(expected_type, std::memory_order_release);
                registered = true;
                break;
            }
        }
        if (!registered) throw std::runtime_error("节点容量已满！");
    }

    virtual ~SoftBusNode() {
        stop(); 
        if (base_addr_ != MAP_FAILED) {
            header_->node_registered[my_id_].store(false);
            munmap(base_addr_, SHM_SIZE);
        }
    }

    virtual void stop() {
        stop_flag_.store(true, std::memory_order_release);
        if (base_addr_ != MAP_FAILED && header_) {
            pthread_mutex_lock(&header_->wake_mutexes[my_id_]);
            pthread_cond_signal(&header_->wake_conds[my_id_]); 
            pthread_mutex_unlock(&header_->wake_mutexes[my_id_]);
        }
    }

    uint32_t get_node_id() const { return my_id_; }

    std::string get_node_name(uint32_t node_id) const {
        if (header_ && node_id < MAX_NODES && header_->node_registered[node_id].load(std::memory_order_acquire)) {
            return std::string(header_->node_names[node_id]);
        }
        return "Unknown";
    }

    [[nodiscard]] int lookup_node(std::string_view target_name, uint32_t send_msg_type) const {
        for (int i = 0; i < MAX_NODES; ++i) {
            if (header_->node_registered[i].load(std::memory_order_acquire) && target_name == header_->node_names[i]) {
                uint32_t target_expected = header_->expected_msg_type[i].load(std::memory_order_acquire);
                if (target_expected == 0 || target_expected == send_msg_type) return i;
                else return -2; 
            }
        }
        return -1; 
    }

    // ========================================================================
    // Pub/Sub API 改造
    // ========================================================================
    void add_route(uint32_t msg_type, int target_id) {
        if (header_ && target_id >= 0 && target_id < MAX_NODES) {
            header_->route_table[msg_type % 256][target_id].store(true, std::memory_order_release);
        }
    }

    void remove_route(uint32_t msg_type, int target_id) {
        if (header_ && target_id >= 0 && target_id < MAX_NODES) {
            header_->route_table[msg_type % 256][target_id].store(false, std::memory_order_release);
        }
    }

    std::vector<int> get_routes(uint32_t msg_type) const {
        std::vector<int> targets;
        if (header_) {
            for (int i = 0; i < MAX_NODES; ++i) {
                if (header_->route_table[msg_type % 256][i].load(std::memory_order_acquire)) {
                    targets.push_back(i);
                }
            }
        }
        return targets;
    }

protected:
    // 将底层推送提取出来
    [[nodiscard]] bool do_push_to_queue(uint32_t tid, uint32_t msg_type, const void* payload, size_t payload_len) {
        if (tid >= MAX_NODES || !header_->node_registered[tid].load(std::memory_order_acquire)) return false;

        void* block = local_cache_.allocate();
        EventData* event = new (block) EventData();
        event->src_id = my_id_;
        event->msg_type = msg_type;
        event->timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        if (payload) nt_memcpy(event->payload, payload, std::min(payload_len, sizeof(event->payload)));

        offset_t offset = local_cache_.get_offset(block);
        if (!header_->rx_queues[tid].push(offset)) {
            local_cache_.deallocate(block);
            return false;
        }

        if (header_->is_sleeping[tid].load(std::memory_order_acquire)) {
            pthread_mutex_lock(&header_->wake_mutexes[tid]);
            pthread_cond_signal(&header_->wake_conds[tid]);
            pthread_mutex_unlock(&header_->wake_mutexes[tid]);
        }
        return true;
    }

    void do_snoop(uint32_t msg_type, const void* payload, size_t payload_len) {
        if (cached_mcp_id_ < 0) {
            if (mcp_lookup_counter_++ % 100 == 0) cached_mcp_id_ = lookup_node("MCPServerBridge", 0);
        }
        if (cached_mcp_id_ >= 0 && (uint32_t)cached_mcp_id_ != my_id_) {
            if (header_->node_registered[cached_mcp_id_].load(std::memory_order_acquire)) {
                // 【核心修复】：强制转换为 (void) 告诉编译器“我确信不需要管嗅探失败的情况”，消除警告
                (void)do_push_to_queue(cached_mcp_id_, msg_type, payload, payload_len);
            } else {
                cached_mcp_id_ = -1; 
            }
        }
    }

    [[nodiscard]] bool internal_send(uint32_t target_id, uint32_t msg_type, const void* payload, size_t payload_len) {
        bool success = do_push_to_queue(target_id, msg_type, payload, payload_len);
        do_snoop(msg_type, payload, payload_len); // 点对点发送时也抄送大模型
        return success;
    }

    // 【核心多播发布】向矩阵中所有订阅者推送
    [[nodiscard]] bool internal_publish(uint32_t msg_type, const void* payload, size_t payload_len) {
        bool sent_any = false;
        if (header_) {
            for (int i = 0; i < MAX_NODES; ++i) {
                if (header_->route_table[msg_type % 256][i].load(std::memory_order_acquire)) {
                    if (do_push_to_queue(i, msg_type, payload, payload_len)) {
                        sent_any = true;
                    }
                }
            }
        }
        do_snoop(msg_type, payload, payload_len); // 大模型上帝视角抄送
        return sent_any;
    }

    void internal_listen() {
        offset_t offset = 0; 
        while (!stop_flag_.load(std::memory_order_acquire)) {
            bool got_msg = false;
            for (int spin_count = 0; spin_count < 1000; ++spin_count) {
                if (stop_flag_.load(std::memory_order_relaxed)) break; 
                if (header_->rx_queues[my_id_].pop(offset)) {
                    got_msg = true;
                    break;
                }
            }

            if (!got_msg && !stop_flag_.load(std::memory_order_acquire)) {
                header_->is_sleeping[my_id_].store(true, std::memory_order_release);
                pthread_mutex_lock(&header_->wake_mutexes[my_id_]);
                if (!stop_flag_.load(std::memory_order_acquire) && !header_->rx_queues[my_id_].pop(offset)) {
                    pthread_cond_wait(&header_->wake_conds[my_id_], &header_->wake_mutexes[my_id_]);
                } else {
                    got_msg = true; 
                }
                pthread_mutex_unlock(&header_->wake_mutexes[my_id_]);
                header_->is_sleeping[my_id_].store(false, std::memory_order_release);
            }

            if (!got_msg && !stop_flag_.load(std::memory_order_acquire)) {
                if (header_->rx_queues[my_id_].pop(offset)) got_msg = true;
            }

            if (got_msg && !stop_flag_.load(std::memory_order_acquire)) {
                EventData* event = local_cache_.get_ptr<EventData>(offset);
                uint64_t now_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
                header_->log_queues[my_id_].push(LogEvent{event->src_id, my_id_, now_ns - event->timestamp});
                
                process_event(event);
                local_cache_.deallocate(event);
            }
        }
    }

    virtual void process_event(const EventData* /*event*/) {}
};

class SourceNode : public SoftBusNode {
public:
    SourceNode(std::string_view name) : SoftBusNode(name, 0) {} 
    [[nodiscard]] bool publish(uint32_t msg_type, const void* payload = nullptr, size_t payload_len = 0) {
        return internal_publish(msg_type, payload, payload_len);
    }
    [[nodiscard]] bool emit(uint32_t target_id, uint32_t msg_type, const void* payload = nullptr, size_t payload_len = 0) {
        return internal_send(target_id, msg_type, payload, payload_len);
    }
};

class NormalNode : public SoftBusNode {
public:
    using Handler = std::function<void(NormalNode* self, const EventData* event)>;
    NormalNode(std::string_view name, uint32_t expected_type = 0) : SoftBusNode(name, expected_type) {}
    void set_handler(Handler h) { handler_ = std::move(h); }
    void run() { internal_listen(); }
    
    [[nodiscard]] bool publish(uint32_t msg_type, const void* payload = nullptr, size_t payload_len = 0) {
        return internal_publish(msg_type, payload, payload_len);
    }
    [[nodiscard]] bool forward(uint32_t target_id, uint32_t msg_type, const void* payload = nullptr, size_t payload_len = 0) {
        return internal_send(target_id, msg_type, payload, payload_len);
    }
protected:
    void process_event(const EventData* event) override {
        if (handler_) handler_(this, event);
    }
private:
    Handler handler_;
};

class SinkNode : public SoftBusNode {
public:
    using Handler = std::function<void(const EventData* event)>;
    SinkNode(std::string_view name, uint32_t expected_type = 0) : SoftBusNode(name, expected_type) {}
    void set_handler(Handler h) { handler_ = std::move(h); }
    void run() { internal_listen(); }
protected:
    void process_event(const EventData* event) override {
        if (handler_) handler_(event);
    }
private:
    Handler handler_;
};

} // namespace shm_bus