#include "framework/softbus_node.h"
#include "business_types.h"
#include <iostream>
#include <string>

using namespace shm_bus;

// 继承 SinkNode，因为我们需要访问底层的 header_ 来查发件人名字
class SmartDashboard : public SinkNode {
public:
    SmartDashboard() : SinkNode("DashboardUI", TYPE_ID(MotorControl)) {
        
        this->set_handler([this](const EventData* event) {
            const auto* data = reinterpret_cast<const MotorControl*>(event->payload);
            
            // 核心高光：从共享内存头部查出这条消息到底是谁发的！
            std::string sender_name = this->header_->node_names[event->src_id];

            if (sender_name == "MCPServerBridge") {
                // 探测到大模型的 Tool 调用指令！
                std::cout << "\n=======================================================\n"
                          << " 🤖 [AI 越权接管] 收到来自云端大模型的最高权限物理指令！\n"
                          << "   -> 目标转速: " << data->speed << " RPM\n"
                          << "   -> 目标扭矩: " << data->torque << " Nm\n"
                          << "   -> 旋转方向: " << (data->direction == 1 ? "正转" : "反转") << "\n"
                          << "=======================================================\n\n";
            } else {
                // 来自 MainController 的正常硬件心跳
                // 我们只打印一个圆点，代表系统活着，但不刷屏
                std::cout << "." << std::flush;
            }
        });
    }
};

int main() {
    std::cout << "=== 工业智能仪表盘 (DashboardUI) 已启动 ===\n";
    std::cout << "正在监听亚微秒级软总线上的电机数据...\n\n";
    
    SmartDashboard dashboard;
    dashboard.run();

    return 0;
}