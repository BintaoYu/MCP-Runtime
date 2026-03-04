#pragma once
#include <atomic>
#include <stdexcept>
#include "../common/shm_types.h"

namespace shm_bus {

// ============================================================================
// 核心数据结构声明
// ============================================================================

// 闲置内存块的侵入式链表节点
struct FreeBlock {
    offset_t next_offset;
};

// 带有版本号的偏移指针，用于 CAS 时防止 ABA 问题
union TaggedOffset {
    uint64_t raw_value;
    struct {
        offset_t offset;
        uint32_t tag;    // 版本号
    } data;
};

// 全局内存池状态 (驻留在共享内存头部)
struct alignas(64) GlobalPoolState {
    std::atomic<uint64_t> head; 
    std::atomic<uint32_t> free_count;
    std::size_t total_blocks;
};

// 全局内存池初始化函数声明
void init_global_pool(void* base_addr, std::size_t total_size, std::size_t data_offset);

// ============================================================================
// 线程本地缓存类声明 (Thread-Local Cache)
// ============================================================================
class ThreadLocalCache {
private:
    void* base_addr_;             
    GlobalPoolState* global_pool_;
    offset_t local_head_;         
    uint32_t local_count_;        
    static constexpr uint32_t BATCH_SIZE = 32; 

public:
    // 构造与析构函数声明
    ThreadLocalCache(void* base_addr);
    ~ThreadLocalCache();

    // 分配与释放接口声明
    void* allocate();
    void deallocate(void* ptr);

    // 物理指针 -> 偏移量
    offset_t get_offset(void* ptr);

    // 【极其重要】：模板函数必须在头文件中实现，否则会报链接错误
    template<typename T>
    T* get_ptr(offset_t offset) {
        if (offset == NULL_OFFSET) return nullptr;
        return reinterpret_cast<T*>(static_cast<char*>(base_addr_) + offset);
    }

private:
    // 核心无锁操作声明
    void fetch_from_global();
    void return_to_global();
};

} // namespace shm_bus