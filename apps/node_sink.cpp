#include "framework/softbus_node.h"
#include "business_types.h"
#include <iostream>
#include <string>

using namespace shm_bus;

int main() {
    SinkNode dashboard("DashboardUI", TYPE_MOTOR_CONTROL);
    std::cout << "[节点C] 仪表盘已上线！等待接收电机指令...\n";

    dashboard.set_handler([&dashboard](const EventData* event) {
        const auto* data = reinterpret_cast<const MotorControl*>(event->payload);
        std::string sender_name = dashboard.get_node_name(event->src_id);
        if (sender_name == "MCPServerBridge") {
            std::cout << "\n 🤖 [AI 越权接管] 收到 MCP 大模型最高权限指令！转速: " << data->speed << " RPM\n";
        } else {
            std::cout << "\n 📺 [仪表盘] 收到来自 [" << sender_name << "] 的电机指令 | 转速: " << data->speed << " RPM\n";
        }
    });

    dashboard.run();
    return 0;
}