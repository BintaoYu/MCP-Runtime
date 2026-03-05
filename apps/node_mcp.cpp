#include "../components/mcp/MCPResourceNode.hpp"
#include "business_types.h"
#include <iostream>
#include <string>
#include <thread>
#include <cstdio>
#include <array>
#include <memory>
#include <sstream>
#include <nlohmann/json.hpp>

using namespace shm_bus;
using json = nlohmann::json;

// 业务层辅助函数：读取 CSV
json read_latency_csv(int tail_lines) {
    std::string command = "tail -n " + std::to_string(tail_lines) + " p2p_latency_trace.csv 2>/dev/null";
    std::array<char, 128> buffer;
    std::string result;
    
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) return json{{"error", "无法读取延迟数据"}};
    
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) result += buffer.data();

    json stats_array = json::array();
    std::istringstream iss(result);
    std::string line;
    
    while (std::getline(iss, line)) {
        if (line.empty() || line.find("source_id") != std::string::npos) continue; 
        size_t p1 = line.find(',');
        size_t p2 = line.find(',', p1 + 1);
        if (p1 != std::string::npos && p2 != std::string::npos) {
            try {
                stats_array.push_back({
                    {"src_id", std::stoi(line.substr(0, p1))},
                    {"dst_id", std::stoi(line.substr(p1 + 1, p2 - p1 - 1))},
                    {"latency_ns", std::stoull(line.substr(p2 + 1))}
                });
            } catch (...) {}
        }
    }
    return stats_array;
}

int main() {
    MCPResourceNode mcp_bridge;

    // ========================================================================
    // 能力 1：注册只读状态 (Resource)
    // ========================================================================
    mcp_bridge.register_type_parser(
        TYPE_ID(MotorControl), "MotorControl", 
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

    // ========================================================================
    // 能力 2：注册系统诊断能力 
    // ========================================================================
    mcp_bridge.register_tool(
        "check_latency",
        "检查底层 P2P 共享内存总线的通信延迟，用于诊断系统是否拥塞",
        json{
            {"type", "object"},
            {"properties", {
                {"lines", {
                    {"type", "integer"}, 
                    {"description", "需要读取的最新记录行数，默认50"}
                }}
            }}
        },
        [](const json& args) -> json {
            int lines = args.value("lines", 50);
            json stats = read_latency_csv(lines);
            // 严格遵循 MCP 规范：内容必须是 array 包裹的对象
            return {{"content", json::array({{{"type", "text"}, {"text", stats.dump(2)}}})}};
        }
    );

    // ========================================================================
    // 能力 3：注册物理控制能力
    // ========================================================================
    mcp_bridge.register_tool(
        "set_motor_state",
        "下发控制指令，调整物理电机的运行参数",
        json{
            {"type", "object"},
            {"properties", {
                {"speed", {{"type", "number"}, {"description", "电机转速 (RPM)"}}},
                {"torque", {{"type", "number"}, {"description", "电机扭矩 (Nm)"}}},
                {"direction", {{"type", "integer"}, {"description", "1为正转，-1为反转"}}}
            }},
            {"required", json::array({"speed", "torque", "direction"})}
        },
        [&mcp_bridge](const json& args) -> json {
            MotorControl cmd;
            cmd.speed = args.value("speed", 0.0f);
            cmd.torque = args.value("torque", 0.0f);
            cmd.direction = args.value("direction", 1);

            int target_id = mcp_bridge.lookup_node("DashboardUI", TYPE_ID(MotorControl));
            if (target_id < 0) {
                return {{"isError", true}, {"content", json::array({{{"type", "text"}, {"text", "发送失败：目标控制节点离线或不存在"}}})}};
            }

            if (mcp_bridge.forward(target_id, TYPE_ID(MotorControl), &cmd, sizeof(cmd))) {
                return {{"content", json::array({{{"type", "text"}, {"text", "硬件指令已成功下发至软总线！"}}})}};
            } else {
                return {{"isError", true}, {"content", json::array({{{"type", "text"}, {"text", "发送失败：总线队列拥塞"}}})}};
            }
        }
    );

    // ========================================================================
    // 启动 JSON-RPC 守护引擎
    // ========================================================================
    std::thread bus_thread([&mcp_bridge]() { mcp_bridge.run(); });
    bus_thread.detach(); 

    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            json req = json::parse(line);
            if (!req.contains("method")) continue;

            bool is_request = req.contains("id");
            std::string method = req["method"];

            // 【核心修复 1】如果这是一条通知（例如 notifications/initialized），绝对不能回复！直接跳过！
            if (!is_request) {
                continue; 
            }

            json res = {{"jsonrpc", "2.0"}, {"id", req["id"]}};
            
            if (method == "initialize") {
                res["result"] = {
                    {"protocolVersion", "2024-11-05"},
                    {"capabilities", {{"resources", json::object()}, {"tools", json::object()}}},
                    {"serverInfo", {{"name", "cxx-softbus-mcp"}, {"version", "1.0.0"}}}
                };
            } 
            // 【核心修复 2】必须响应 ping心跳！否则新版网页端会直接罢工白屏
            else if (method == "ping") {
                res["result"] = json::object(); 
            }
            else if (method == "resources/list") {
                res["result"] = mcp_bridge.mcp_list_resources();
            }
            else if (method == "resources/read") {
                res["result"] = mcp_bridge.mcp_read_resource(req["params"]["uri"].get<std::string>());
            }
            else if (method == "tools/list") {
                res["result"] = mcp_bridge.mcp_list_tools();
            }
            else if (method == "tools/call") {
                std::string name = req["params"]["name"];
                json arguments = req["params"].value("arguments", json::object());
                res["result"] = mcp_bridge.mcp_call_tool(name, arguments);
            }
            else {
                res["error"] = {{"code", -32601}, {"message", "Method not found"}};
            }

            std::cout << res.dump() << std::endl;
        } catch (...) {
            // JSON 解析失败时静默，防止输出脏文本导致通信通道污染
        }
    }

    return 0;
}