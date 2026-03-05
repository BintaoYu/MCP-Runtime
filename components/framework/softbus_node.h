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
    for (; i + 8 <= size; i += 8) {
        _mm_stream_si64(reinterpret_cast<long long*>(d_64++), *s_64++);
    }
    char* d_c = reinterpret_cast<char*>(d_64);
    const char* s_c = reinterpret_cast<const char*>(s_64);
    for (; i < size; ++i) {
        *d_c++ = *s_c++;
    }
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

    // 【新增】底层流量镜像：上帝视角的大模型网关寻址缓存
    int cached_mcp_id_ = -1;
    uint32_t mcp_lookup_counter_ = 0;

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
        if (base_addr_ != MAP_FAILED) {
            header_->node_registered[my_id_].store(false);
            munmap(base_addr_, SHM_SIZE);
        }
    }

    uint32_t get_node_id() const { return my_id_; }

    [[nodiscard]] int lookup_node(std::string_view target_name, uint32_t send_msg_type) const {
        for (int i = 0; i < MAX_NODES; ++i) {
            if (header_->node_registered[i].load(std::memory_order_acquire) && 
                target_name == header_->node_names[i]) {
                uint32_t target_expected = header_->expected_msg_type[i].load(std::memory_order_acquire);
                if (target_expected == 0 || target_expected == send_msg_type) {
                    return i;
                } else {
                    return -2; 
                }
            }
        }
        return -1; 
    }

protected:
    [[nodiscard]] bool internal_send(uint32_t target_id, uint32_t msg_type, const void* payload, size_t payload_len) {
        if (target_id >= MAX_NODES || !header_->node_registered[target_id].load()) return false;

        // 【重构核心】：将内存申请、拷贝和入队封装为复用的 Lambda 函数
        auto do_push_to_queue = [&](uint32_t tid) -> bool {
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
        };

        // 1. 标准流程：将数据发送给真正指定的业务目标节点
        bool success = do_push_to_queue(target_id);

        // ========================================================================
        // 2. 架构级特性：透明总线嗅探 (Transparent Bus Snooping)
        // ========================================================================
        // 定期去寻找系统里是否存在大模型 MCP 网关（避免每发一条都全盘遍历，大幅降低开销）
        if (cached_mcp_id_ < 0) {
            if (mcp_lookup_counter_++ % 100 == 0) {
                cached_mcp_id_ = lookup_node("MCPServerBridge", 0);
            }
        }

        // 如果上帝网关存在，且当前消息的目标本来就不是网关（防止死循环发两次），且当前节点也不是网关自己
        if (cached_mcp_id_ >= 0 && (uint32_t)cached_mcp_id_ != target_id && (uint32_t)cached_mcp_id_ != my_id_) {
            // 检查大模型是否还活着
            if (header_->node_registered[cached_mcp_id_].load(std::memory_order_acquire)) {
                // 神不知鬼不觉地申请一块新内存，把数据镜像复制一份丢给大模型！
                do_push_to_queue(cached_mcp_id_);
            } else {
                cached_mcp_id_ = -1; // 大模型掉线了，清除缓存
            }
        }

        return success;
    }

    void internal_listen() {
        offset_t offset;
        while (true) {
            bool got_msg = false;
            for (int spin_count = 0; spin_count < 1000; ++spin_count) {
                if (header_->rx_queues[my_id_].pop(offset)) {
                    got_msg = true;
                    break;
                }
            }

            if (!got_msg) {
                header_->is_sleeping[my_id_].store(true, std::memory_order_release);
                pthread_mutex_lock(&header_->wake_mutexes[my_id_]);
                if (!header_->rx_queues[my_id_].pop(offset)) {
                    pthread_cond_wait(&header_->wake_conds[my_id_], &header_->wake_mutexes[my_id_]);
                } else {
                    got_msg = true; 
                }
                pthread_mutex_unlock(&header_->wake_mutexes[my_id_]);
                header_->is_sleeping[my_id_].store(false, std::memory_order_release);
            }

            if (!got_msg && header_->rx_queues[my_id_].pop(offset)) got_msg = true;

            if (got_msg) {
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