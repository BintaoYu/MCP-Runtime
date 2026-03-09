#include "framework/softbus_node.h"
#include "business_types.h"
#include <iostream>

using namespace shm_bus;

int main() {
    // 启动一个监听 SensorData 类型的普通节点
    NormalNode controller("AutomatedController", TYPE_ID(SensorData));
    std::cout << "🧠 自动化控制节点已启动，监听传感器数据...\n";

    controller.set_handler([&controller](NormalNode* self, const EventData* event) {
        const auto* sensor = reinterpret_cast<const SensorData*>(event->payload);
        
        // 简单的边缘计算逻辑：温度过高时，加速电机散热
        MotorControl cmd{1000.0f, 2.5f, 1}; // 默认转速
        if (sensor->temperature > 30.0f) {
            cmd.speed = 3000.0f; // 狂暴模式
        }

        // 1. 发送电机控制指令给底层的硬件节点
        (void)self->publish(TYPE_ID(MotorControl), &cmd, sizeof(cmd));
        
        // 2. 同步更新电机的“期望状态”到无锁缓存，供 MCP 随时查阅
        self->put_state(TYPE_ID(MotorControl), &cmd, sizeof(cmd));
    });

    controller.run(); // 阻塞运行，利用条件变量极致休眠
    return 0;
}