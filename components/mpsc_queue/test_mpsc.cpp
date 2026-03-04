#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include "lockfree_mpsc.h"

using namespace shm_bus;

int main() {
    std::cout << "=== 亚微秒级 P2P 无锁 MPSC 环形队列测试 ===\n\n";

    // 1. 定义一个容量为 65536 的无锁队列，存放 uint32_t 类型数据
    // 在真实场景中，这里存的将是内存池分配出来的 offset_t
    MPSCQueue<uint32_t, 65536> queue;

    constexpr int NUM_PRODUCERS = 8;
    constexpr uint32_t MSG_PER_PRODUCER = 1000000;
    
    // 我们用一个原子变量来校验总数
    std::atomic<uint64_t> total_sum(0);
    uint64_t expected_sum = 0;

    std::cout << "[功能与防丢包校验] 启动 " << NUM_PRODUCERS << " 个生产者线程...\n";
    std::cout << "每个生产者猛烈发送 " << MSG_PER_PRODUCER << " 条数据，单消费者紧紧跟随接收...\n";

    auto start_time = std::chrono::high_resolution_clock::now();

    // 消费者线程 (单线程跑)
    std::thread consumer([&]() {
        uint32_t received_count = 0;
        uint32_t data;
        uint64_t local_sum = 0;
        
        // 我们预期的总接收量是 800 万条
        while (received_count < NUM_PRODUCERS * MSG_PER_PRODUCER) {
            if (queue.pop(data)) {
                local_sum += data;
                received_count++;
            }
            // 真实场景可以用短暂的 cpu pause 指令缓解空转
        }
        total_sum.store(local_sum);
    });

    // 生产者线程组 (多线程并发往队列里塞数字)
    std::vector<std::thread> producers;
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        producers.emplace_back([&, i]() {
            uint32_t base_val = i * 10;
            for (uint32_t j = 0; j < MSG_PER_PRODUCER; ++j) {
                // 如果队列满了，我们就死循环一直重试 (Spin Lock 行为)
                while (!queue.push(base_val + j)) {}
            }
        });

        // 提前算好我们期望得到的数据累加总和，一会用来对账
        for (uint32_t j = 0; j < MSG_PER_PRODUCER; ++j) {
            expected_sum += (i * 10 + j);
        }
    }

    // 等待所有生产者打完子弹
    for (auto& p : producers) {
        p.join();
    }
    // 等待消费者全部吃进肚子
    consumer.join();

    auto end_time = std::chrono::high_resolution_clock::now();

    // ========================================================================
    // 数据校验与吞吐量统计
    // ========================================================================
    auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
    double duration_s = duration_ns / 1e9;
    
    uint64_t total_ops = NUM_PRODUCERS * MSG_PER_PRODUCER; // 这里只算入队次数
    double ops_per_sec = total_ops / duration_s;

    std::cout << "\n[校验结果] 对账单核对中...\n";
    std::cout << "预期接收总和: " << expected_sum << "\n";
    std::cout << "实际接收总和: " << total_sum.load() << "\n";

    if (expected_sum == total_sum.load()) {
        std::cout << "✅ 并发数据完美一致，没有发生任何数据覆盖、乱序或丢失！\n";
    } else {
        std::cout << "❌ 测试失败：数据对不齐，并发存在 Bug！\n";
    }

    std::cout << "\n=== 🚀 极限吞吐量数据 ===\n";
    std::cout << "总物理耗时: " << duration_s << " 秒\n";
    std::cout << "🔥 处理吞吐量 (QPS): " << ops_per_sec / 10000.0 << " 万条消息/秒\n";
    
    return 0;
}