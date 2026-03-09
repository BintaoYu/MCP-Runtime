#include "framework/softbus_node.h"
#include "business_types.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace shm_bus;

int main() {
    SourceNode sensor("TempSensor");
    std::cout << "🌡️ 传感器节点已启动，开始高频采集数据...\n";

    float temp = 25.0f;
    while (true) {
        SensorData data{temp, 60.0f};
        
        // 1. 发送流式事件给订阅了该主题的节点 (走 MPSC 无锁队列)
        // 注意：TYPE_ID 宏里不要加双引号，直接传类型名即可
        (void)sensor.publish(TYPE_ID(SensorData), &data, sizeof(data));
        
        // 2. 将最新状态写入全局高并发缓存，供 MCP 引擎 (大模型) 零延迟读取
        sensor.put_state(TYPE_ID(SensorData), &data, sizeof(data));

        // 模拟温度变化
        temp += 0.2f;
        if (temp > 35.0f) temp = 25.0f;

        // 模拟 10Hz 的高频采样率
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}