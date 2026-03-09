#include "mcp/MCPEngine.hpp"
#include "mcp/ToolRegistry.hpp"
#include "business_types.h"
#include <iostream>

using namespace shm_bus;

int main() {
    // 1. 初始化无状态的 MCP 网关引擎
    MCPEngine engine("cxx-softbus-mcp", "1.1.0");

    // 2. 绑定底层无锁缓存 (O(1)极速查询，无需订阅队列)
    engine.bind_cache_resource<SensorData>(
        "SensorData", TYPE_ID("SensorData"), 
        [](const SensorData* data) {
            return json{{"temperature_c", data->temperature}, {"humidity_percent", data->humidity}};
        }
    );

    engine.bind_cache_resource<MotorControl>(
        "MotorControl", TYPE_ID("MotorControl"), 
        [](const MotorControl* data) {
            return json{{"speed_rpm", data->speed}, {"torque_nm", data->torque}, {"direction", data->direction}};
        }
    );

    // 3. 一键挂载所有注册的工具算子
    ToolRegistry::register_all(engine);

    // 4. 启动协议主循环 (自带防线防崩溃)
    engine.run_blocking();

    return 0;
}