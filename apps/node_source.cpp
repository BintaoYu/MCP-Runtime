// apps/node_source.cpp
#include "framework/softbus_node.h"
#include "business_types.h"
#include <unistd.h>
using namespace shm_bus;
const int T = 10000; // 发送 10 条消息就退出


int main() {
    SourceNode controller("MainController");
    
    // 寻址时出示准备发送的类型 ID
    int target_id = controller.lookup_node("DashboardUI", TYPE_ID(MotorControl));
    
    if (target_id < 0) {
        std::cerr << "寻址失败，错误码: " << target_id << "\n";
        return 1;
    }

    // 构造业务数据
    MotorControl cmd = {1500.5f, 30.2f, 1};

    int t = 0;
    while (t < T) {
        if (!controller.emit(target_id, TYPE_ID(MotorControl), &cmd, sizeof(cmd))) {
            std::cerr << "发送指令失败：目标节点队列已满或已离线。\n";
        } 
        else {
            std::cout << "指令发送成功！\n";
        }
        t++;
        usleep(1000); // 每 1ms 发一条
    }
    return 0;
}