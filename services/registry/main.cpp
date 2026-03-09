#include <iostream>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include "common/shm_layout.h"
#include "allocator/shm_allocator.h"

using namespace shm_bus;

int main() {
    std::cout << "=== SoftBus Registry (Pub/Sub Controller) 启动 ===\n";

    shm_unlink(SHM_NAME);

    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) return 1;
    if (ftruncate(shm_fd, SHM_SIZE) == -1) return 1;
    void* base_addr = mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (base_addr == MAP_FAILED) return 1;

    ShmHeader* header = new (base_addr) ShmHeader();

    // 【修复】：正确的传参顺序 (总大小在前，数据偏移量在后)
    init_global_pool(base_addr, SHM_SIZE, sizeof(ShmHeader));

    for (uint32_t i = 0; i < MAX_NODES; ++i) {
        header->node_registered[i].store(false, std::memory_order_relaxed);
        pthread_mutexattr_t mutex_attr;
        pthread_mutexattr_init(&mutex_attr);
        pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(&header->wake_mutexes[i], &mutex_attr);

        pthread_condattr_t cond_attr;
        pthread_condattr_init(&cond_attr);
        pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
        pthread_cond_init(&header->wake_conds[i], &cond_attr);
        
        header->is_sleeping[i].store(false, std::memory_order_relaxed);
    }

    // 初始化 Pub/Sub 订阅矩阵，默认全部断开
    for (int src = 0; src < MAX_NODES; ++src) {
        for (int type_idx = 0; type_idx < 256; ++type_idx) {
            for (int dst = 0; dst < MAX_NODES; ++dst) {
                header->route_table[src][type_idx][dst].store(false, std::memory_order_relaxed);
            }
        }
    }

    std::cout << "注册中心就绪，总线容量: " << SHM_SIZE / (1024 * 1024) << " MB\n";
    std::cout << "Pub/Sub 矩阵初始化完毕，等待云端大模型下发订阅指令...\n";

    while (true) sleep(10);
    return 0;
}