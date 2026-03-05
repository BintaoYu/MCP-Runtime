#include "framework/softbus_node.h"
#include "business_types.h"
#include <unistd.h>
using namespace shm_bus;

const int T = 10000; 

int main() {
    SourceNode controller("MainController");
    
    std::cout << "[Source] 正在寻找 DashboardUI 节点...\n";
    int target_id = -1;
    // 【优化】循环等待，直到 Sink 节点上线
    while ((target_id = controller.lookup_node("DashboardUI", TYPE_ID(MotorControl))) < 0) {
        usleep(100000); // 等待 100ms 后重试
    }
    std::cout << "[Source] 寻址成功！目标节点 ID: " << target_id << "\n";

    MotorControl cmd = {1500.5f, 30.2f, 1};

    int t = 0;
    while (t < T) {
        if (!controller.emit(target_id, TYPE_ID(MotorControl), &cmd, sizeof(cmd))) {
            std::cerr << "发送指令失败：目标节点队列已满。\n";
        }
        t++;
        // 【优化】对于云服务器，每 10 毫秒发一条即可，不要发得太疯狂
        usleep(10000); 
    }
    
    std::cout << "[Source] 10000 条指令发送完毕，安全退出。\n";
    return 0;
}