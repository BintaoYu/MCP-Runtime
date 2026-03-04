#include <iostream>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fstream>
#include <sched.h>
#include <pthread.h>
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

    // 绑定到系统的最后一个逻辑核
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

    // 预分配大块缓存，防止写 CSV 时发生频繁的磁盘 I/O 碎片
    std::ofstream csv_file("p2p_latency_trace.csv", std::ios::trunc);
    const size_t file_buf_size = 1024 * 1024; // 1MB 内存写缓冲
    char* file_buffer = new char[file_buf_size];
    csv_file.rdbuf()->pubsetbuf(file_buffer, file_buf_size);
    
    csv_file << "source_id,target_id,latency_ns\n";

    std::cout << "[Logger] 正在深层静默监听 " << MAX_NODES << " 个功能块节点的延迟指标...\n";

    LogEvent log_ev;
    uint64_t flush_counter = 0;
    
    // 死循环旁路收割
    while (true) {
        bool idle = true;
        
        // 扫街：轮询所有节点的监控信箱
        for (int i = 0; i < MAX_NODES; ++i) {
            // 一次性掏空当前节点积压的全部日志
            while (header->log_queues[i].pop(log_ev)) {
                idle = false;
                csv_file << log_ev.src_id << "," 
                         << log_ev.dst_id << "," 
                         << log_ev.latency_ns << "\n";
                flush_counter++;
            }
        }

        // 批量刷盘策略：每收集满 50000 条，将内存 buffer 刷入磁盘
        if (flush_counter >= 50000) {
            csv_file.flush();
            flush_counter = 0;
        }

        // 如果整整一圈扫下来，512 个节点一条新日志都没有
        if (idle) {
            // 旁路进程妥协：主动休眠 100 微秒，让出总线带宽，防止把内存带宽打满
            // 这种设计在不影响核心业务的前提下，极大降低了系统的整体功耗
            usleep(100); 
            
            // 闲暇时顺手把没满的 buffer 也刷进磁盘
            if (flush_counter > 0) {
                csv_file.flush();
                flush_counter = 0;
            }
        }
    }

    delete[] file_buffer;
    return 0;
}