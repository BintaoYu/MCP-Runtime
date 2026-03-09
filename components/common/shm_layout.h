#pragma once

#include <cstdint>
#include <atomic>
#include <pthread.h>
#include "common/shm_types.h"
#include "allocator/shm_allocator.h"
#include "mpsc_queue/lockfree_mpsc.h"

namespace shm_bus {

constexpr int MAX_NODES = 512;                          
constexpr std::size_t QUEUE_CAPACITY = 8192;            
constexpr const char* SHM_NAME = "/p2p_bus_shm";        
constexpr std::size_t SHM_SIZE = 256 * 1024 * 1024;     

struct EventData {
    uint32_t src_id;        
    uint32_t msg_type;      
    uint64_t timestamp;     
    char payload[236];      
};

struct LogEvent {
    uint32_t src_id;       
    uint32_t dst_id;       
    uint64_t latency_ns;   
};

// ============================================================================
// 高并发无锁缓存系统 (Lock-free Shm Cache)
// ============================================================================

struct CacheNode {
    uint32_t key_hash;                      
    uint64_t timestamp;                     
    offset_t payload_offset;                
    std::atomic<offset_t> next_node_offset; 
};

template <std::size_t BucketCount = 1024>
class LockFreeShmHashMap {
private:
    std::atomic<offset_t> buckets_[BucketCount];

public:
    LockFreeShmHashMap() {
        for (std::size_t i = 0; i < BucketCount; ++i) {
            buckets_[i].store(NULL_OFFSET, std::memory_order_relaxed);
        }
    }

    std::size_t get_bucket_index(uint32_t key_hash) const {
        return key_hash & (BucketCount - 1); 
    }

    std::atomic<offset_t>* get_buckets() { return buckets_; }
};

// ============================================================================
// 共享内存绝对头部布局 (ShmHeader)
// ============================================================================
struct ShmHeader {
    GlobalPoolState pool_state; 

    std::atomic<bool> node_registered[MAX_NODES];
    std::atomic<bool> is_sleeping[MAX_NODES]; 
    char node_names[MAX_NODES][32];
    std::atomic<uint32_t> expected_msg_type[MAX_NODES];
    pthread_mutex_t wake_mutexes[MAX_NODES];
    pthread_cond_t  wake_conds[MAX_NODES];

    MPSCQueue<offset_t, QUEUE_CAPACITY> rx_queues[MAX_NODES];
    MPSCQueue<LogEvent, QUEUE_CAPACITY> log_queues[MAX_NODES];

    std::atomic<bool> route_table[MAX_NODES][256][MAX_NODES];

    // 【全新架构核心】：挂载全局高并发状态缓存树
    LockFreeShmHashMap<1024> global_state_cache;
};

} // namespace shm_bus