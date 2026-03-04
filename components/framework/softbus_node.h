#pragma once

#include <iostream>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <chrono>
#include <functional>
// 引入 x86/x64 硬件指令集
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
    
    // 每次 8 字节直接刷入物理内存，不经过 L1/L2/L3
    for (; i + 8 <= size; i += 8) {
        _mm_stream_si64(reinterpret_cast<long long*>(d_64++), *s_64++);
    }
    
    // 处理剩余的尾巴字节
    char* d_c = reinterpret_cast<char*>(d_64);
    const char* s_c = reinterpret_cast<const char*>(s_64);
    for (; i < size; ++i) {
        *d_c++ = *s_c++;
    }
    
    // 内存屏障：强制要求这批旁路缓存的数据立刻对所有核心可见
    _mm_sfence(); 
#else
    // 如果是 ARM 等非 x86 架构，优雅降级为普通拷贝
    std::memcpy(dst, src, size);
#endif
}

// ============================================================================
// 核心基类：提供底层的连接、收发与休眠能力
// ============================================================================
class SoftBusNode {
protected:
    uint32_t my_id_;                 
    void* base_addr_;                
    ShmHeader* header_;              
    ThreadLocalCache local_cache_;   

public:
    SoftBusNode(const char* my_name, uint32_t expected_type = 0) 
        : base_addr_(MAP_FAILED), header_(nullptr), local_cache_(nullptr) {
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
                std::strncpy(header_->node_names[my_id_], my_name, 31);
                header_->node_names[my_id_][31] = '\0'; // 确保安全截断
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

    // 返回值：>= 0 表示成功的节点 ID；-1 表示未找到；-2 表示类型不匹配拒绝连接
    int lookup_node(const char* target_name, uint32_t send_msg_type) const {
        for (int i = 0; i < MAX_NODES; ++i) {
            if (header_->node_registered[i].load(std::memory_order_acquire) && 
                std::strcmp(header_->node_names[i], target_name) == 0) {
                // 获取目标节点期望的类型
                uint32_t target_expected = header_->expected_msg_type[i].load(std::memory_order_acquire);
                // 类型握手校验：目标接收任意类型 (0)，或者类型严格一致
                if (target_expected == 0 || target_expected == send_msg_type) {
                    return i;
                } else {
                    return -2; // 存在该节点，但数据类型不兼容，拦截潜在的内存越界
                }
            }
        }
        return -1; 
    }
    uint32_t get_node_id() const { return my_id_; }

protected:
    // 底层发送逻辑 (带按需唤醒)
    bool internal_send(uint32_t target_id, uint32_t msg_type, const void* payload, size_t payload_len) {
        if (target_id >= MAX_NODES || !header_->node_registered[target_id].load()) return false;

        void* block = local_cache_.allocate();
        EventData* event = new (block) EventData();
        event->src_id = my_id_;
        event->msg_type = msg_type;
        event->timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        if (payload) {
            // std::memcpy(event->payload, payload, std::min(payload_len, sizeof(event->payload)));
            nt_memcpy(event->payload, payload, std::min(payload_len, sizeof(event->payload)));
        }
        offset_t offset = local_cache_.get_offset(block);
        
        if (!header_->rx_queues[target_id].push(offset)) {
            local_cache_.deallocate(block);
            return false;
        }

        // 如果目标节点睡着了，跨进程敲门叫醒它！
        if (header_->is_sleeping[target_id].load(std::memory_order_acquire)) {
            pthread_mutex_lock(&header_->wake_mutexes[target_id]);
            pthread_cond_signal(&header_->wake_conds[target_id]);
            pthread_mutex_unlock(&header_->wake_mutexes[target_id]);
        }
        return true;
    }

    // 底层混合事件泵 (Hybrid Event Pump)
    void internal_listen() {
        std::cout << "[SoftBus] 节点 " << my_id_ << " 进入事件驱动休眠模式 (0% CPU Idle)...\n";
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

            if (!got_msg && header_->rx_queues[my_id_].pop(offset)) {
                got_msg = true;
            }

            if (got_msg) {
                EventData* event = local_cache_.get_ptr<EventData>(offset);
                
                // 旁路日志采集
                uint64_t now_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
                header_->log_queues[my_id_].push(LogEvent{event->src_id, my_id_, now_ns - event->timestamp});
                
                // 触发多态业务
                process_event(event);

                local_cache_.deallocate(event);
            }
        }
    }

    // 【核心修复】：在这里提供空的默认实现，彻底干掉旧的纯虚函数
    virtual void process_event(const EventData* /*event*/) {}
};

// ============================================================================
// 角色 1：事件源 (SourceNode) - 只发不收
// ============================================================================
class SourceNode : public SoftBusNode {
public:
    SourceNode(const char* name) : SoftBusNode(name, 0) {}
    bool emit(uint32_t target_id, uint32_t msg_type, const void* payload = nullptr, size_t payload_len = 0) {
        return internal_send(target_id, msg_type, payload, payload_len);
    }
};

// ============================================================================
// 角色 2：处理节点 (NormalNode) - 收到事件 -> 运算 -> 发给下游
// ============================================================================
class NormalNode : public SoftBusNode {
public:
    NormalNode(const char* name, uint32_t expected_type = 0) : SoftBusNode(name, expected_type) {}
    // 定义业务回调函数的签名：把自身指针传给业务，方便业务调用 forward 转发
    using Handler = std::function<void(NormalNode* self, const EventData* event)>;

    // 绑定业务逻辑回调
    void set_handler(Handler h) { handler_ = std::move(h); }
    
    void run() { internal_listen(); }
    
    // 向下游转发数据
    bool forward(uint32_t target_id, uint32_t msg_type, const void* payload = nullptr, size_t payload_len = 0) {
        return internal_send(target_id, msg_type, payload, payload_len);
    }

protected:
    void process_event(const EventData* event) override {
        // 底层收到数据后，直接触发用户绑定的 Lambda 回调
        if (handler_) handler_(this, event);
    }

private:
    Handler handler_;
};

// ============================================================================
// 角色 3：接收终端 (SinkNode) - 只收不发
// ============================================================================
class SinkNode : public SoftBusNode {
public:
    SinkNode(const char* name, uint32_t expected_type = 0) : SoftBusNode(name, expected_type) {}
    // Sink 不需要向外转发，所以回调签名更简单
    using Handler = std::function<void(const EventData* event)>;

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