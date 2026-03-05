#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>

namespace shm_bus {

constexpr bool is_power_of_two(std::size_t n) {
    return (n != 0) && ((n & (n - 1)) == 0);
}

template <typename T, std::size_t Capacity>
class MPSCQueue {
    static_assert(is_power_of_two(Capacity), "为了极致性能，队列容量 Capacity 必须是 2 的幂次方！");

private:
    struct Cell {
        std::atomic<std::size_t> sequence; 
        T data; 
    };

    static constexpr std::size_t CACHE_LINE_SIZE = 64;
    static constexpr std::size_t MASK = Capacity - 1;

    alignas(CACHE_LINE_SIZE) Cell buffer_[Capacity];
    alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> enqueue_pos_;
    alignas(CACHE_LINE_SIZE) std::size_t dequeue_pos_;

public:
    MPSCQueue() {
        for (std::size_t i = 0; i < Capacity; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
        enqueue_pos_.store(0, std::memory_order_relaxed);
        dequeue_pos_ = 0;
    }

    MPSCQueue(const MPSCQueue&) = delete;
    MPSCQueue& operator=(const MPSCQueue&) = delete;

    bool push(const T& data) {
        Cell* cell = nullptr;
        std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);

        for (;;) {
            cell = &buffer_[pos & MASK];
            std::size_t seq = cell->sequence.load(std::memory_order_acquire);
            std::intptr_t diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);

            if (diff == 0) {
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break; 
                }
            } else if (diff < 0) {
                return false; 
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }

        cell->data = data;
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& data) {
        Cell* cell = &buffer_[dequeue_pos_ & MASK];
        std::size_t seq = cell->sequence.load(std::memory_order_acquire);
        
        std::intptr_t diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(dequeue_pos_ + 1);

        if (diff == 0) {
            data = cell->data;
            cell->sequence.store(dequeue_pos_ + Capacity, std::memory_order_release);
            dequeue_pos_++;
            return true;
        }

        return false;
    }
};

} // namespace shm_bus