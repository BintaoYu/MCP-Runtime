#include "framework/softbus_node.h"
#include "business_types.h"
#include <iostream>

using namespace shm_bus;

int main() {
    NormalNode processor("AI_Controller", TYPE_SENSOR_DATA);
    std::cout << "[节点B] AI 控制器已上线！等待接收温度数据...\n";

    processor.set_handler([&processor](NormalNode* self, const EventData* event) {
        const auto* sensor = reinterpret_cast<const SensorData*>(event->payload);
        
        MotorControl cmd;
        cmd.speed = sensor->temperature * 50.0f; 
        cmd.torque = 10.5f;
        cmd.direction = 1;

        std::cout << "\n [AI大脑] 收到温度: " << sensor->temperature 
                  << "℃ -> 计算出电机转速: " << cmd.speed << " RPM. 正在向外盲发指令...";

        // 【核心修复】：规范接收 [[nodiscard]] 的返回值，并增加状态提示
        bool routed = self->publish(TYPE_MOTOR_CONTROL, &cmd, sizeof(cmd));
        if (!routed) {
            std::cout << " (提示: 链路断开，数据落入黑洞)";
        }
    });

    processor.run();
    return 0;
}