#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>

namespace shm_bus {

// 这是一个利用 C++ 模板元编程计算数字是否为 2 的幂次方的工具
constexpr bool is_power_of_two(std::size_t n) {
    return (n != 0) && ((n & (n - 1)) == 0);
}

/**
 * 亚微秒级多生产者单消费者 (MPSC) 无锁环形队列
 * * 核心原理 (Sequence Buffer 算法)：
 * 就像去银行排队叫号。每个“座位(Cell)”都有一个动态的“叫号屏(sequence)”。
 * - 生产者(Producer)想要坐下，得先看叫号屏上的号码和自己手里的号码对不对得上。
 * 对上了才能坐下放数据，放完后把叫号屏上的数字 +1，通知消费者来取。
 * - 消费者(Consumer)取完数据后，把叫号屏的数字 + 容量，通知下一波生产者可以覆盖这个座位了。
 * * @tparam T 存放的数据类型 (在我们的框架里，通常存的是 offset_t)
 * @tparam Capacity 队列容量，必须是 2 的幂次方！(如 1024, 2048) 这样可以用位运算 & 代替 % 取模，速度快 10 倍。
 */
template <typename T, std::size_t Capacity>
class MPSCQueue {
    static_assert(is_power_of_two(Capacity), "为了极致性能，队列容量 Capacity 必须是 2 的幂次方！");

private:
    // 队列中的每一个“座位”
    struct Cell {
        // 原子序列号，用于同步生产者和消费者。
        std::atomic<std::size_t> sequence; 
        T data; 
    };

    // 缓存行大小，现代 x86 CPU 通常是 64 字节
    static constexpr std::size_t CACHE_LINE_SIZE = 64;
    // 掩码，用于将一直自增的游标转换为 0 ~ Capacity-1 的数组索引
    static constexpr std::size_t MASK = Capacity - 1;

    // ========================================================================
    // 核心成员变量：使用 alignas(64) 强制隔离！
    // 为什么？如果 enqueue 的 tail 和 dequeue 的 head 挨在一起，
    // CPU 在多核并发时会导致缓存行频繁失效互相踢出，这种现象叫“伪共享(False Sharing)”，会使性能暴跌。
    // ========================================================================

    // 存储数据的环形数组
    alignas(CACHE_LINE_SIZE) Cell buffer_[Capacity];

    // 生产者的写入游标 (多个线程疯狂抢占这个变量，所以是 atomic)
    alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> enqueue_pos_;

    // 消费者的读取游标 (单消费者，只有自己用，按理不需要 atomic，但为了配合序列号计算，保持一致)
    alignas(CACHE_LINE_SIZE) std::size_t dequeue_pos_;

public:
    MPSCQueue() {
        // 初始化每个座位的序列号。第 i 个座位初始号码就是 i
        for (std::size_t i = 0; i < Capacity; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
        enqueue_pos_.store(0, std::memory_order_relaxed);
        dequeue_pos_ = 0;
    }

    // 禁止拷贝
    MPSCQueue(const MPSCQueue&) = delete;
    MPSCQueue& operator=(const MPSCQueue&) = delete;

    /**
     * 生产者入队操作 (多线程并发安全)
     * @param data 要压入的数据
     * @return true 成功, false 队列已满
     */
    bool push(const T& data) {
        Cell* cell = nullptr;
        std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);

        // 死循环抢占一个可写的槽位
        for (;;) {
            // 利用掩码计算出真实的数组索引
            cell = &buffer_[pos & MASK];
            
            // 获取当前座位的序列号
            // memory_order_acquire 保证在这个读取动作之后的所有读写，都不会被编译器乱序排到它前面去
            std::size_t seq = cell->sequence.load(std::memory_order_acquire);
            
            // 巧妙的差值计算：
            // 如果差值为 0，说明这个座位刚好轮到现在的 pos 来写，它是空的！
            std::intptr_t diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);

            if (diff == 0) {
                // 座位是对的！尝试把 enqueue_pos_ + 1，如果成功了，说明我抢到了这个座位！
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break; // 抢座成功，跳出循环准备写数据
                }
            } else if (diff < 0) {
                // 如果序列号落后于当前的 pos，说明队列被前一圈的数据塞满了，还没被消费者读走
                return false; 
            } else {
                // 差值大于 0，说明有其他生产者抢跑了，我们更新自己的 pos 重试
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }

        // 已经抢到独占的座位了，安心把数据放进去
        cell->data = data;
        
        // 告诉消费者：这个座位的数据已经准备好了！
        // release 语义保证：在修改序列号之前，cell->data 的写入动作绝对已经彻底完成，对别的核心可见！
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    /**
     * 消费者出队操作 (只允许单线程调用)
     * @param data 引用，用于带出取到的数据
     * @return true 成功, false 队列为空
     */
    bool pop(T& data) {
        Cell* cell = &buffer_[dequeue_pos_ & MASK];
        std::size_t seq = cell->sequence.load(std::memory_order_acquire);
        
        // 期望的序列号是当前读游标 + 1 (因为生产者写完后把它改成了 pos + 1)
        std::intptr_t diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(dequeue_pos_ + 1);

        if (diff == 0) {
            // 完美对上！说明数据已经被完全写好了，读出来！
            data = cell->data;
            // 读完以后，把座位的序列号设置为下一圈生产者来的时候期望看到的数字
            // 也就是当前位置 + 队列总容量
            cell->sequence.store(dequeue_pos_ + Capacity, std::memory_order_release);
            // 读取游标往前走一步
            dequeue_pos_++;
            return true;
        }

        // 差值小于 0，说明生产者还没写到这，队列是空的
        return false;
    }
};

} // namespace shm_bus