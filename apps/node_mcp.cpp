#include "../components/mcp/MCPResourceNode.hpp"
#include "business_types.h"
#include <iostream>
#include <string>
#include <thread>
#include <nlohmann/json.hpp>

using namespace shm_bus;
using json = nlohmann::json;

int main() {
    MCPResourceNode mcp_bridge;

    // 注册 MotorControl 解析逻辑
    mcp_bridge.register_type_parser(
        TYPE_ID(MotorControl), 
        "MotorControl", 
        [](const EventData* event) -> json {
            const auto* data = reinterpret_cast<const MotorControl*>(event->payload);
            return json{
                {"speed_rpm", data->speed},
                {"torque_nm", data->torque},
                {"direction", data->direction == 1 ? "Forward" : "Reverse"},
                {"timestamp_ns", event->timestamp}
            };
        }
    );

    // 启动后台线程运行软总线监听
    std::thread bus_thread([&mcp_bridge]() {
        mcp_bridge.run(); 
    });
    bus_thread.detach(); // 分离线程，让主线程专注处理 LLM 的 JSON 请求

    // ========================================================================
    // 核心：MCP 协议 JSON-RPC 2.0 事件循环 (Stdio 通信)
    // ========================================================================
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            json req = json::parse(line);
            
            // 忽略没有 id 或 method 的非标准请求
            if (!req.contains("method")) continue;

            json res = {{"jsonrpc", "2.0"}};
            if (req.contains("id")) {
                res["id"] = req["id"];
            }
            
            std::string method = req["method"];

            // 1. 初始化握手：告诉大模型我具备什么能力
            if (method == "initialize") {
                res["result"] = {
                    {"protocolVersion", "2024-11-05"},
                    {"capabilities", {{"resources", json::object()}}},
                    {"serverInfo", {{"name", "cxx-softbus-mcp"}, {"version", "1.0.0"}}}
                };
            } 
            // 2. 握手确认：静默放行
            else if (method == "notifications/initialized") {
                continue; 
            } 
            // 3. 资源发现请求
            else if (method == "resources/list") {
                res["result"] = mcp_bridge.mcp_list_resources();
            } 
            // 4. 资源读取请求
            else if (method == "resources/read") {
                std::string uri = req["params"]["uri"];
                res["result"] = mcp_bridge.mcp_read_resource(uri);
            } 
            // 未知方法兜底
            else {
                res["error"] = {{"code", -32601}, {"message", "Method not found"}};
            }

            // 【极度关键】：MCP 协议要求必须是单行 JSON 且以换行符结尾，必须 flush
            std::cout << res.dump() << std::endl;

        } catch (...) {
            // 捕获所有异常，防止畸形 JSON 导致网关崩溃
        }
    }

    return 0;
}