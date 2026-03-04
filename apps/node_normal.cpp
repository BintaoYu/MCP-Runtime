#include <iostream>
#include <string>
#include "framework/softbus_node.h"
#include "business_types.h"
using namespace shm_bus;

int main() {
    // 1. 上线并注册名称
    NormalNode filter("FilterLogic"); 
    
    // 2. 寻址：找下游的 Dashboard 在哪里
    int target_id = -1;
    while (target_id == -1) {
        target_id = filter.lookup_node("DashboardUI", TYPE_ID(MotorControl));
        if (target_id == -1 || target_id == -2) {
            if (target_id == -1)
                std::cout << "等待 DashboardUI 上线...\n";
            else
                std::cout << "DashboardUI类型不匹配\n";
            usleep(500000); // 找不到就等半秒再查
        }
    }

    std::cout << "已找到 DashboardUI，其底层 ID 为: " << target_id << "\n";

    // 3. 注入业务并转发
    filter.set_handler([target_id](NormalNode* self, const EventData* event) {
        std::string processed = std::string(event->payload) + " -> [Filtered]";
        self->forward(target_id, 200, processed.c_str(), processed.size() + 1);
    });
    filter.run(); 
}