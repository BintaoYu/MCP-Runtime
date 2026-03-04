#include <iostream>
#include "framework/softbus_node.h"
#include "business_types.h"
using namespace shm_bus;

int main() {
    // 注册时严格声明只接收 MotorControl 类型的数据
    SinkNode dashboard("DashboardUI", TYPE_ID(MotorControl)); 
    
    dashboard.set_handler([](const EventData* event) {
        // 由于底层的类型握手已拦截了非法类型，这里可以直接安全强转
        const MotorControl* cmd = reinterpret_cast<const MotorControl*>(event->payload);
        
        std::cout << "[Dashboard] 收到电机控制指令:\n"
                  << "  速度: " << cmd->speed << "\n"
                  << "  扭矩: " << cmd->torque << "\n"
                  << "  方向: " << cmd->direction << "\n";
    });

    dashboard.run(); 
    return 0;
}