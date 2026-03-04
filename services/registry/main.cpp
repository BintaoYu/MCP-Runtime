#include <iostream>
#include <fcntl.h>      // For O_* constants
#include <sys/mman.h>   // For shm_open, mmap
#include <unistd.h>     // For ftruncate, sleep
#include <csignal>
#include <cstring>
#include "common/shm_layout.h"

using namespace shm_bus;

// 全局指针，用于在捕获 Ctrl+C 时清理内存
void* g_base_addr = MAP_FAILED; 

// 优雅退出的信号处理函数
void handle_sigint(int /*sig*/) {
    std::cout << "\n[Registry] 收到退出信号，正在销毁共享内存...\n";
    if (g_base_addr != MAP_FAILED) {
        munmap(g_base_addr, SHM_SIZE);
    }
    shm_unlink(SHM_NAME); // 从 /dev/shm 彻底删除文件
    std::cout << "[Registry] 物理底座已清理完毕，安全退出。\n";
    exit(0);
}

int main() {
    std::cout << "=== P2P 控制面：注册中心 (Registry) 启动 ===\n";

    // 注册 Ctrl+C 信号捕获
    std::signal(SIGINT, handle_sigint);

    // 1. 在 /dev/shm 下创建共享物理内存文件
    // O_CREAT: 如果不存在则创建; O_RDWR: 读写权限
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("[ERROR] shm_open 失败");
        return 1;
    }

    // 2. 将文件截断到我们设定的 256MB 大小
    if (ftruncate(shm_fd, SHM_SIZE) == -1) {
        perror("[ERROR] ftruncate 失败");
        return 1;
    }

    // 3. 将这 256MB 物理内存映射到当前进程的虚拟地址空间
    g_base_addr = mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, shm_fd, 0);
    if (g_base_addr == MAP_FAILED) {
        perror("[ERROR] mmap 失败");
        return 1;
    }

    // mmap 成功后，文件描述符就可以关掉了，内存映射会继续保持
    close(shm_fd); 

    std::cout << "[Registry] 成功开辟物理内存 (" << SHM_SIZE / 1024 / 1024 << " MB)。\n";
    std::cout << "[Registry] 正在进行物理布局格式化...\n";

    // 4. 将映射到的内存强转为我们定义的全局头部布局
    ShmHeader* header = static_cast<ShmHeader*>(g_base_addr);

    // ========================================================================
    // 极其硬核的 C++ 技巧：定位 new (Placement New)
    // 普通的 new 会去堆上分配内存。而 Placement New 允许我们指定内存地址！
    // 我们强制在这块共享内存上调用 MPSCQueue 的构造函数，初始化 512 个信箱。
    // ========================================================================
    for (int i = 0; i < MAX_NODES; ++i) {
        // 1. 强制在指定内存实例化业务接收信箱
        new (&header->rx_queues[i]) MPSCQueue<offset_t, QUEUE_CAPACITY>();
        
        // 2. 强制在指定内存实例化旁路日志队列 (如果你之前加上了的话)
        new (&header->log_queues[i]) MPSCQueue<LogEvent, QUEUE_CAPACITY>();
        
        // 3. 将全局路由表中该槽位的注册状态全部重置为 false (未占用)
        header->node_registered[i].store(false, std::memory_order_relaxed); 
        header->is_sleeping[i].store(false, std::memory_order_relaxed); // 初始化醒着
        std::memset(header->node_names[i], 0, 32);
        header->expected_msg_type[i].store(0, std::memory_order_relaxed);
        // 【新增】：初始化跨进程共享的 Mutex 和 Cond
        pthread_mutexattr_t mattr;
        pthread_mutexattr_init(&mattr);
        pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED); // 极其重要：允许跨进程
        pthread_mutex_init(&header->wake_mutexes[i], &mattr);
        pthread_mutexattr_destroy(&mattr);

        pthread_condattr_t cattr;
        pthread_condattr_init(&cattr);
        pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);  // 极其重要：允许跨进程
        pthread_cond_init(&header->wake_conds[i], &cattr);
        pthread_condattr_destroy(&cattr);
    }
    std::cout << "[Registry] " << MAX_NODES << " 个无锁信道初始化完毕。\n";

    // 5. 初始化全局内存池
    // 传入 sizeof(ShmHeader)，告诉内存池：数据块从 512 个信箱后面开始切分！
    init_global_pool(g_base_addr, SHM_SIZE, sizeof(ShmHeader));
    
    auto* pool_state = &header->pool_state;
    std::cout << "[Registry] Slab 内存池切分完毕，共生成 " << pool_state->total_blocks << " 个可用的事件数据块。\n";
    std::cout << "[Registry] 物理底座搭建完毕！全部服务处于 Ready 状态。\n\n";

    std::cout << "[Registry] 守护进程运行中... (按 Ctrl+C 安全退出)\n";

    // 控制面挂起，充当常驻进程。未来这里可以增加节点健康检查(心跳)逻辑。
    while (true) {
        sleep(10); 
    }

    return 0;
}