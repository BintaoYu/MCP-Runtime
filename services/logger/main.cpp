#include <iostream>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fstream>
#include <sched.h>
#include <pthread.h>
#include <thread>   
#include <chrono>   
#include <memory>   
#include "common/shm_layout.h"

using namespace shm_bus;

void pin_to_core(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0) {
        std::cout << "[Logger] 成功锚定 CPU 核心 " << core_id << "\n";
    }
}

int main() {
    std::cout << "=== P2P 旁路监控服务 (轻量服务器模式) ===\n";

    long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    // 【核心修复 1】只有在 4 核及以上机器才进行物理绑核，防止 2 核小鸡卡死
    if (num_cores >= 4) {
        pin_to_core(num_cores - 1); 
    } else {
        std::cout << "[Logger] 检测到轻量级云环境，已自动关闭物理绑核。\n";
    }

    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd < 0) return 1;
    void* base_addr = mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd);
    
    ShmHeader* header = static_cast<ShmHeader*>(base_addr);

    std::ofstream csv_file("p2p_latency_trace.csv", std::ios::trunc);
    constexpr size_t file_buf_size = 1024 * 1024; 
    auto file_buffer = std::make_unique<char[]>(file_buf_size);
    csv_file.rdbuf()->pubsetbuf(file_buffer.get(), file_buf_size);
    csv_file << "source_id,target_id,latency_ns\n";

    LogEvent log_ev;
    uint64_t flush_counter = 0;
    
    while (true) {
        bool idle = true;
        
        for (int i = 0; i < MAX_NODES; ++i) {
            while (header->log_queues[i].pop(log_ev)) {
                idle = false;
                csv_file << log_ev.src_id << "," << log_ev.dst_id << "," << log_ev.latency_ns << "\n";
                flush_counter++;
            }
        }

        if (flush_counter >= 10000) { // 降低刷盘阈值
            csv_file.flush();
            flush_counter = 0;
        }

        if (idle) {
            // 【核心修复 2】休眠 10 毫秒！极大降低 CPU 负载！
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
            if (flush_counter > 0) {
                csv_file.flush();
                flush_counter = 0;
            }
        }
    }
    return 0;
}