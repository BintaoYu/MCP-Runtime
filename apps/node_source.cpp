#include "framework/softbus_node.h"
#include "business_types.h"
#include <unistd.h>
#include <iostream>

using namespace shm_bus;

int main() {
    SourceNode sensor("TempSensor"); 
    
    std::cout << "[节点A] 温度传感器已上线！只负责输出数据 (Type: 1)\n";
    SensorData data = {25.0f, 50.0f};
 
    while (true) {
        // 完全不关心发给谁，向总线抛出 TYPE_SENSOR_DATA
        bool routed = sensor.publish(TYPE_SENSOR_DATA, &data, sizeof(data));
        
        if (routed) std::cout << "." << std::flush;
        else std::cout << "x" << std::flush;

        data.temperature += 0.5f;
        if (data.temperature > 100.0f) data.temperature = 25.0f;
        usleep(1000000); 
    }
    return 0;
}