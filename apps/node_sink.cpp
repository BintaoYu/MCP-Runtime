#include "framework/softbus_node.h"
#include "business_types.h"
#include <iostream>

using namespace shm_bus;

int main() {
    // 启动一个专门监听 MotorControl 类型的接收节点
    SinkNode motor("MotorHardware", TYPE_ID(MotorControl));
    std::cout << "⚙️ 电机执行节点已启动，等待控制指令...\n";

    motor.set_handler([](const EventData* event) {
        const auto* cmd = reinterpret_cast<const MotorControl*>(event->payload);
        
        // 打印接收到的指令，模拟硬件控制
        // 由于是从无锁队列 pop 出来的，这里的延迟可以逼近亚微秒级
        std::cout << "[硬件执行] 收到电机指令 -> 转速: " << cmd->speed 
                  << " RPM, 扭矩: " << cmd->torque 
                  << " Nm, 转向: " << cmd->direction << "\n";
    });

    motor.run();
    return 0;
}