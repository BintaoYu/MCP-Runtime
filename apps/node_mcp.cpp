#include "mcp/MCPEngine.hpp"
#include "mcp/ToolRegistry.hpp"
#include "business_types.h"
#include <iostream>

using namespace shm_bus;

int main() {
    // 1. 初始化无状态的 MCP 网关引擎
    MCPEngine engine("cxx-softbus-mcp", "1.1.0");

    // 注册 AI 观测路由：打通大模型 URI 到底层纯二进制缓存的 O(1) 寻址与 JSON 自动翻译
    REG_MCP_CACHE(engine, SensorData);
    REG_MCP_CACHE(engine, MotorControl);
    // REG_MCP_CACHE_NAMED(engine, "LivingRoom_Temp", SensorData); 
    // 3. 一键挂载所有注册的工具算子
    ToolRegistry::register_all(engine);

    // 4. 启动协议主循环 (自带防线防崩溃)
    engine.run_blocking();

    return 0;
}