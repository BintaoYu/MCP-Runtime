#include "framework/softbus_node.h"
#include "business_types.h"
#include <unistd.h>
#include <iostream>

using namespace shm_bus;

int main() {
    SourceNode controller("MainController");
    
    std::cout << "[硬件] 正在寻找总线上的节点...\n";
    int target_id = -1;
    while ((target_id = controller.lookup_node("DashboardUI", TYPE_ID(MotorControl))) < 0) {
        usleep(100000); 
    }
    std::cout << "[硬件] 寻址成功！仪表盘节点 ID: " << target_id << "\n";

    MotorControl cmd = {1500.5f, 30.2f, 1};

    while (true) {
        // 【极度纯净】业务代码完全剥离 MCP 的侵入，只负责给自己真正的业务目标发指令！
        if (!controller.emit(target_id, TYPE_ID(MotorControl), &cmd, sizeof(cmd))) {
            std::cerr << "[硬件] 发送指令失败：目标节点队列已满。\n";
        }
        
        cmd.speed += 0.5f;
        if (cmd.speed > 1600.0f) cmd.speed = 1500.0f;

        usleep(1000000); 
    }
    
    return 0;
}