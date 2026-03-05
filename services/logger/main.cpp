#include <iostream>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fstream>
#include <sched.h>
#include <pthread.h>
#include <thread>   // 【C++11/17】现代线程休眠
#include <chrono>   // 【C++11/17】现代时间库
#include <memory>   // 【C++11/17】智能指针
#include "common/shm_layout.h"

using namespace shm_bus;

// 强制绑核：隔离操作系统的调度干扰
void pin_to_core(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        std::cerr << "[Logger] 警告: 绑核失败，请确保您有足够的权限或该 CPU 核心存在。\n";
    } else {
        std::cout << "[Logger] 成功锚定 CPU 核心 " << core_id << "，业务层干扰已物理隔绝。\n";
    }
}

int main() {
    std::cout << "=== P2P 旁路监控服务 (Observability Reaper) 启动 ===\n";

    long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_cores > 1) {
        pin_to_core(num_cores - 1); 
    }

    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("[Logger] 总线未就绪 (shm_open failed)");
        return 1;
    }
    void* base_addr = mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd);
    
    ShmHeader* header = static_cast<ShmHeader*>(base_addr);

    // 【C++17 优化】使用 RAII 智能指针管理 I/O 缓冲区，彻底告别 new/delete 的内存泄漏风险
    std::ofstream csv_file("p2p_latency_trace.csv", std::ios::trunc);
    constexpr size_t file_buf_size = 1024 * 1024; // 1MB 内存写缓冲
    auto file_buffer = std::make_unique<char[]>(file_buf_size);
    csv_file.rdbuf()->pubsetbuf(file_buffer.get(), file_buf_size);
    
    csv_file << "source_id,target_id,latency_ns\n";

    std::cout << "[Logger] 正在深层静默监听 " << MAX_NODES << " 个功能块节点的延迟指标...\n";

    LogEvent log_ev;
    uint64_t flush_counter = 0;
    
    while (true) {
        bool idle = true;
        
        for (int i = 0; i < MAX_NODES; ++i) {
            while (header->log_queues[i].pop(log_ev)) {
                idle = false;
                csv_file << log_ev.src_id << "," 
                         << log_ev.dst_id << "," 
                         << log_ev.latency_ns << "\n";
                flush_counter++;
            }
        }

        if (flush_counter >= 50000) {
            csv_file.flush();
            flush_counter = 0;
        }

        if (idle) {
            // 【C++17 优化】使用现代 chrono 和 thread 库替代古老的 usleep
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            
            if (flush_counter > 0) {
                csv_file.flush();
                flush_counter = 0;
            }
        }
    }

    // 无需手动 delete[]，离开作用域 unique_ptr 自动清理
    return 0;
}