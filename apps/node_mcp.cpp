#include "mcp/MCPResourceNode.hpp"
#include "business_types.h" // 包含 MotorControl 定义
#include <iostream>
#include <thread>

using namespace shm_bus;

int main() {
    MCPResourceNode mcp_bridge;

    // 1. 注册 MotorControl 的解析逻辑 (反射与序列化)
    mcp_bridge.register_type_parser(
        TYPE_ID(MotorControl), 
        "MotorControl", 
        [](const EventData* event) -> json {
            // 极限强转，零拷贝读取
            const auto* data = reinterpret_cast<const MotorControl*>(event->payload);
            // 组装呈现给 LLM 的 JSON
            return json{
                {"speed_rpm", data->speed},
                {"torque_nm", data->torque},
                {"direction", data->direction == 1 ? "Forward" : "Reverse"},
                {"timestamp_ns", event->timestamp}
            };
        }
    );

    // 2. 启动一个独立线程运行软总线的事件泵 (0% CPU 挂起监听)
    std::thread bus_thread([&mcp_bridge]() {
        std::cout << "[MCP Bridge] 连接至底层工业总线，监听中...\n";
        mcp_bridge.run(); 
    });

    // 3. 模拟 MCP Server 的异步 HTTP/JSON-RPC 请求 (主线程)
    // 在真实的 MCP Server 中，这里将是对接 stdio 或 SSE 连接的代码
    std::this_thread::sleep_for(std::chrono::seconds(2)); // 等待数据积累
    
    std::cout << "\n--- LLM 请求: 发现资源 (resources/list) ---\n";
    std::cout << mcp_bridge.mcp_list_resources().dump(2) << "\n";

    std::cout << "\n--- LLM 请求: 读取数据 (resources/read) ---\n";
    std::cout << mcp_bridge.mcp_read_resource("bus://types/MotorControl/latest").dump(2) << "\n";

    bus_thread.join();
    return 0;
}