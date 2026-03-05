#pragma once

#include <cstdint>
#include <atomic>
#include <pthread.h>
#include "common/shm_types.h"
#include "allocator/shm_allocator.h"
#include "mpsc_queue/lockfree_mpsc.h"

namespace shm_bus {

// ============================================================================
// 全局架构常量定义
// ============================================================================
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

    // Pub/Sub 订阅矩阵 (256个主题 x MAX_NODES个节点)
    std::atomic<bool> route_table[256][MAX_NODES];
};

} // namespace shm_bus