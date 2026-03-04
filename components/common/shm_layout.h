#pragma once

#include <cstdint>
#include "common/shm_types.h"
#include "allocator/shm_allocator.h"
#include "mpsc_queue/lockfree_mpsc.h"

namespace shm_bus {

// ============================================================================
// 全局架构常量定义
// ============================================================================
constexpr int MAX_NODES = 512;                          // 支持的最大业务节点数
constexpr std::size_t QUEUE_CAPACITY = 8192;            // 每个节点的信箱容量 (必须是2的幂)
constexpr const char* SHM_NAME = "/p2p_bus_shm";        // 共享内存的文件名
constexpr std::size_t SHM_SIZE = 256 * 1024 * 1024;     // 共享内存总大小：256 MB

// ============================================================================
// 业务事件实体 (存放在 SlabBlock 数据区中)
// ============================================================================
// 这是未来真正在节点之间流转的数据结构。
// 为了极致性能，它必须是 POD 类型 (Plain Old Data)，不能包含 std::string 等隐式指针！
struct EventData {
    uint32_t src_id;        // 发送方节点 ID
    uint32_t msg_type;      // 消息业务类型
    uint64_t timestamp;     // 打点时间戳 (用于计算延迟)
    char payload[236];      // 业务数据，故意设为 236 字节，使得整个结构体刚好 256 字节 (BLOCK_SIZE)
};

struct LogEvent {
    uint32_t src_id;       // 谁发来的
    uint32_t dst_id;       // 谁接收的
    uint64_t latency_ns;   // 端到端延迟 (纳秒)
};

// ============================================================================
// 共享内存绝对头部布局 (ShmHeader)
// ============================================================================
// 这个结构体强制固定了内存的前半部分长什么样。
struct ShmHeader {
    GlobalPoolState pool_state;

    // 全局动态路由注册表。true 表示该槽位已被某个节点占用
    std::atomic<bool> node_registered[MAX_NODES];

    // 标记某个节点是否正在深度睡眠，避免发送端无脑发信号拖慢性能
    std::atomic<bool> is_sleeping[MAX_NODES]; 
    // 跨进程互斥锁与条件变量数组
    //全局节点的 DNS 电话簿 (每个名字最长 31 字符)
    char node_names[MAX_NODES][32];
    std::atomic<uint32_t> expected_msg_type[MAX_NODES];
    pthread_mutex_t wake_mutexes[MAX_NODES];
    pthread_cond_t  wake_conds[MAX_NODES];

    // 业务数据的接收信箱
    MPSCQueue<offset_t, QUEUE_CAPACITY> rx_queues[MAX_NODES];
    // 旁路日志的无锁队列
    MPSCQueue<LogEvent, QUEUE_CAPACITY> log_queues[MAX_NODES];
};
};


