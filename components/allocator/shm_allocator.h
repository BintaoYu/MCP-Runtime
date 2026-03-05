#pragma once
#include <atomic>
#include <stdexcept>
#include "../common/shm_types.h"

namespace shm_bus {

struct FreeBlock {
    offset_t next_offset;
};

union TaggedOffset {
    uint64_t raw_value;
    struct {
        offset_t offset;
        uint32_t tag;    
    } data;
};

struct alignas(64) GlobalPoolState {
    std::atomic<uint64_t> head; 
    std::atomic<uint32_t> free_count;
    std::size_t total_blocks;
};

void init_global_pool(void* base_addr, std::size_t total_size, std::size_t data_offset);

class ThreadLocalCache {
private:
    void* base_addr_;             
    GlobalPoolState* global_pool_;
    offset_t local_head_;         
    uint32_t local_count_;        
    static constexpr uint32_t BATCH_SIZE = 32; 

public:
    ThreadLocalCache(void* base_addr);
    ~ThreadLocalCache();

    void* allocate();
    void deallocate(void* ptr);

    offset_t get_offset(void* ptr);

    template<typename T>
    T* get_ptr(offset_t offset) {
        if (offset == NULL_OFFSET) return nullptr;
        return reinterpret_cast<T*>(static_cast<char*>(base_addr_) + offset);
    }

private:
    void fetch_from_global();
    void return_to_global();
};

} // namespace shm_bus