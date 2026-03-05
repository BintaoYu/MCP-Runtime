#include "shm_allocator.h"
#include <iostream>

namespace shm_bus {

void init_global_pool(void* base_addr, std::size_t total_size, std::size_t data_offset) {
    if (total_size < data_offset + BLOCK_SIZE) {
        throw std::runtime_error("共享内存太小，装不下元数据！");
    }
    GlobalPoolState* pool = static_cast<GlobalPoolState*>(base_addr);
    std::size_t available_size = total_size - data_offset; 
    pool->total_blocks = available_size / BLOCK_SIZE;
    pool->free_count.store(pool->total_blocks, std::memory_order_relaxed);

    char* data_start = static_cast<char*>(base_addr) + data_offset; 

    for (std::size_t i = 0; i < pool->total_blocks - 1; ++i) {
        char* current_block_addr = data_start + (i * BLOCK_SIZE);
        FreeBlock* block = reinterpret_cast<FreeBlock*>(current_block_addr);
        block->next_offset = data_offset + ((i + 1) * BLOCK_SIZE); 
    }
    
    char* last_block_addr = data_start + ((pool->total_blocks - 1) * BLOCK_SIZE);
    reinterpret_cast<FreeBlock*>(last_block_addr)->next_offset = NULL_OFFSET;

    TaggedOffset initial_head;
    initial_head.data.offset = data_offset; 
    initial_head.data.tag = 0; 
    pool->head.store(initial_head.raw_value, std::memory_order_release);
}

ThreadLocalCache::ThreadLocalCache(void* base_addr) 
    : base_addr_(base_addr), 
      global_pool_(static_cast<GlobalPoolState*>(base_addr)),
      local_head_(NULL_OFFSET),
      local_count_(0) {}

ThreadLocalCache::~ThreadLocalCache() {
    while (local_head_ != NULL_OFFSET) {
        return_to_global(); 
    }
}

void* ThreadLocalCache::allocate() {
    if (local_head_ == NULL_OFFSET) {
        fetch_from_global();
    }
    if (local_head_ == NULL_OFFSET) {
        throw std::bad_alloc(); 
    }

    offset_t current_offset = local_head_;
    FreeBlock* block = get_ptr<FreeBlock>(current_offset);
    local_head_ = block->next_offset; 
    local_count_--;
    return block; 
}

void ThreadLocalCache::deallocate(void* ptr) {
    if (!ptr) return;
    offset_t offset = get_offset(ptr);
    FreeBlock* block = static_cast<FreeBlock*>(ptr);
    
    block->next_offset = local_head_;
    local_head_ = offset;
    local_count_++;

    if (local_count_ >= BATCH_SIZE * 2) {
        return_to_global();
    }
}

offset_t ThreadLocalCache::get_offset(void* ptr) {
    if (!ptr) return NULL_OFFSET;
    return static_cast<offset_t>(static_cast<char*>(ptr) - static_cast<char*>(base_addr_));
}

void ThreadLocalCache::fetch_from_global() {
    TaggedOffset expected_head, new_head;
    do {
        expected_head.raw_value = global_pool_->head.load(std::memory_order_acquire);
        if (expected_head.data.offset == NULL_OFFSET) return; 

        FreeBlock* first_block = get_ptr<FreeBlock>(expected_head.data.offset);
        new_head.data.offset = first_block->next_offset;
        new_head.data.tag = expected_head.data.tag + 1; 
    } while (!global_pool_->head.compare_exchange_weak(
                expected_head.raw_value, new_head.raw_value,
                std::memory_order_release, std::memory_order_relaxed));

    FreeBlock* grabbed_block = get_ptr<FreeBlock>(expected_head.data.offset);
    grabbed_block->next_offset = local_head_;
    local_head_ = expected_head.data.offset;
    local_count_++;
    global_pool_->free_count.fetch_sub(1, std::memory_order_relaxed);
}

void ThreadLocalCache::return_to_global() {
    if (local_head_ == NULL_OFFSET) return;

    offset_t return_offset = local_head_;
    FreeBlock* return_block = get_ptr<FreeBlock>(return_offset);

    local_head_ = return_block->next_offset;
    local_count_--;

    TaggedOffset expected_head, new_head;
    do {
        expected_head.raw_value = global_pool_->head.load(std::memory_order_acquire);
        return_block->next_offset = expected_head.data.offset;
        new_head.data.offset = return_offset;
        new_head.data.tag = expected_head.data.tag + 1;
    } while (!global_pool_->head.compare_exchange_weak(
                expected_head.raw_value, new_head.raw_value,
                std::memory_order_release, std::memory_order_relaxed));

    global_pool_->free_count.fetch_add(1, std::memory_order_relaxed);
}

} // namespace shm_bus