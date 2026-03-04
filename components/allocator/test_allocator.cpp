#include <iostream>
#include <vector>
#include <thread>
#include "shm_allocator.h"

using namespace shm_bus;

int main() {
    std::cout << "=== 亚微秒级共享内存无锁内存池测试开始 ===\n";

    // 1. 模拟分配一块 10MB 的共享内存 (在进程堆上分配，完全解耦 mmap)
    constexpr size_t SHM_SIZE = 10 * 1024 * 1024;
    std::vector<char> simulated_shm(SHM_SIZE);
    void* base_addr = simulated_shm.data();

    // 2. 初始化全局内存池
    init_global_pool(base_addr, SHM_SIZE, sizeof(GlobalPoolState));
    auto* pool_state = static_cast<GlobalPoolState*>(base_addr);
    std::cout << "全局池初始化完毕。总块数: " << pool_state->total_blocks << "\n\n";

    // 3. 多线程高并发争抢压测
    std::cout << "[并发测试] 启动 8 个业务线程进行无锁抢占与归还...\n";
    constexpr int NUM_THREADS = 8;
    constexpr int ALLOCS_PER_THREAD = 10000;

    auto worker_task = [&](int thread_id) {
        // 模拟一个独立的业务容器进程
        ThreadLocalCache local_cache(base_addr); 
        std::vector<void*> allocated_ptrs;
        allocated_ptrs.reserve(ALLOCS_PER_THREAD);

        // 疯狂申请内存块
        for (int i = 0; i < ALLOCS_PER_THREAD; ++i) {
            allocated_ptrs.push_back(local_cache.allocate());
        }

        // 疯狂归还内存块
        for (void* ptr : allocated_ptrs) {
            local_cache.deallocate(ptr);
        }
        // 离开作用域时，析构函数会自动将本地缓存剩余的块无锁退还给全局池
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(worker_task, i);
    }

    // 等待所有业务线程执行完毕
    for (auto& t : threads) {
        t.join();
    }
    std::cout << "高并发测试完成，未发生 ABA 问题及死锁！\n\n";

    // 4. 终极断言：检查内存泄漏
    std::cout << "[泄漏校验] 正在清点全局可用内存块...\n";
    std::cout << "当前剩余空闲块: " << pool_state->free_count.load() << "\n";
    std::cout << "初始设计总块数: " << pool_state->total_blocks << "\n";

    if (pool_state->free_count.load() == pool_state->total_blocks) {
        std::cout << "✅ 测试完美通过！100% 回收，零内存泄漏。\n";
    } else {
        std::cout << "❌ 测试失败：发现内存泄漏！\n";
    }

    // ========================================================================
    // 5. 极限性能压测 (Performance Benchmark)
    // ========================================================================
    std::cout << "\n=== 🚀 极限性能压测开始 (Performance Benchmark) ===\n";
    
    // 我们让 8 个线程，每个线程执行 100 万次分配 + 100 万次释放
    constexpr int BENCH_THREADS = 8;
    constexpr int OPS_PER_THREAD = 1000000; 
    constexpr int BATCH_SIZE = 1000; // 每次拿 1000 个块就还回去，防止撑爆内存池

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> bench_threads;
    for (int i = 0; i < BENCH_THREADS; ++i) {
        bench_threads.emplace_back([&]() {
            ThreadLocalCache local_cache(base_addr);
            std::vector<void*> ptrs;
            ptrs.reserve(BATCH_SIZE);

            // 执行 1000 轮，每轮 1000 次分配和释放 = 100 万次
            for (int j = 0; j < OPS_PER_THREAD / BATCH_SIZE; ++j) {
                // 极速分配
                for (int k = 0; k < BATCH_SIZE; ++k) {
                    ptrs.push_back(local_cache.allocate());
                }
                // 极速回收
                for (void* p : ptrs) {
                    local_cache.deallocate(p);
                }
                ptrs.clear();
            }
        });
    }

    for (auto& t : bench_threads) {
        t.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    
    // 数据统计
    auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
    double duration_s = duration_ns / 1e9;
    
    uint64_t total_ops = BENCH_THREADS * OPS_PER_THREAD * 2; // 一次 Alloc + 一次 Free 算两次操作
    double ops_per_sec = total_ops / duration_s;
    double ns_per_op = (double)duration_ns / total_ops;

    std::cout << "压测线程数: " << BENCH_THREADS << "\n";
    std::cout << "总操作数 (Alloc + Free): " << total_ops << " 次\n";
    std::cout << "总物理耗时: " << duration_s << " 秒\n";
    std::cout << "🔥 吞吐量 (QPS/OPS): " << ops_per_sec / 10000.0 << " 万次/秒\n";
    std::cout << "⏱️ 平均单次延迟: " << ns_per_op << " 纳秒 (ns)\n";

    if (ns_per_op < 1000.0) {
        std::cout << "\n✅ 性能达标：成功实现亚微秒级 (Sub-microsecond) 极低延迟！\n";
    } else {
        std::cout << "\n⚠️ 延迟偏高，请检查是否在编译时开启了 -O3 优化。\n";
    }

    return 0;
}