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
#include <csignal> // 【健壮性补丁】：引入信号处理头文件

using namespace shm_bus;
using json = nlohmann::json;

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
    // 【健壮性补丁】：忽略 SIGPIPE 信号，防止网页关闭/管道断开时 C++ 进程崩溃
    std::signal(SIGPIPE, SIG_IGN);

    MCPResourceNode mcp_bridge;

    mcp_bridge.register_type_parser(TYPE_SENSOR_DATA, "SensorData", [](const EventData* event) -> json {
        const auto* data = reinterpret_cast<const SensorData*>(event->payload);
        return json{{"temperature_c", data->temperature}, {"humidity_percent", data->humidity}};
    });

    mcp_bridge.register_type_parser(TYPE_MOTOR_CONTROL, "MotorControl", [](const EventData* event) -> json {
        const auto* data = reinterpret_cast<const MotorControl*>(event->payload);
        return json{{"speed_rpm", data->speed}, {"torque_nm", data->torque}, {"direction", data->direction}};
    });

    mcp_bridge.register_tool(
        "get_bus_topology",
        "获取当前底层的 Pub/Sub 路由拓扑表，查看各主题有哪些节点正在订阅",
        json{{"type", "object"}, {"properties", json::object()}},
        [&mcp_bridge](const json& /*args*/) -> json {
            json routes = json::array();
            for (int msg_type = 0; msg_type < 256; ++msg_type) {
                std::vector<int> subscribers = mcp_bridge.get_routes(msg_type);
                if (!subscribers.empty()) {
                    json subs_json = json::array();
                    for (int tid : subscribers) {
                        subs_json.push_back({
                            {"target_node_id", tid},
                            {"target_node_name", mcp_bridge.get_node_name(tid)}
                        });
                    }
                    routes.push_back({
                        {"msg_type", msg_type},
                        {"subscribers", subs_json}
                    });
                }
            }
            if (routes.empty()) return {{"content", json::array({{{"type", "text"}, {"text", "当前路由表为空，没有任何节点产生订阅。"}}})}};
            return {{"content", json::array({{{"type", "text"}, {"text", routes.dump(2)}}})}};
        }
    );

    mcp_bridge.register_tool(
        "update_bus_topology",
        "【全局网管】配置动态 Pub/Sub 路由表。动作支持：'connect'(接通) 或 'disconnect'(断开)。主题：1=SensorData, 2=MotorControl",
        json{
            {"type", "object"},
            {"properties", {
                {"action", {{"type", "string"}, {"description", "操作: 'connect' 或 'disconnect'"}}},
                {"msg_type", {{"type", "integer"}, {"description", "数据类型ID"}}},
                {"target_node_name", {{"type", "string"}, {"description", "操作的目标节点名称"}}}
            }},
            {"required", json::array({"action", "msg_type", "target_node_name"})}
        },
        [&mcp_bridge](const json& args) -> json {
            std::string action = args.value("action", "connect");
            int msg_type = args.value("msg_type", -1);
            std::string target_name = args.value("target_node_name", "");
            
            if (msg_type < 0 || msg_type > 255) return {{"isError", true}, {"content", json::array({{{"type", "text"}, {"text", "非法的 msg_type"}}})}};
            
            int target_id = mcp_bridge.lookup_node(target_name, msg_type);
            if (target_id < 0) return {{"isError", true}, {"content", json::array({{{"type", "text"}, {"text", "路由失败：目标离线或期望的数据类型不匹配！"}}})}};

            if (action == "connect") {
                mcp_bridge.add_route(msg_type, target_id);
                return {{"content", json::array({{{"type", "text"}, {"text", "🔗 节点 [" + target_name + "] 成功订阅了 Topic: " + std::to_string(msg_type)}}})}};
            } else if (action == "disconnect") {
                mcp_bridge.remove_route(msg_type, target_id);
                return {{"content", json::array({{{"type", "text"}, {"text", "✂️ 节点 [" + target_name + "] 成功退订了 Topic: " + std::to_string(msg_type)}}})}};
            } else {
                return {{"isError", true}, {"content", json::array({{{"type", "text"}, {"text", "非法的 action"}}})}};
            }
        }
    );

    mcp_bridge.register_tool(
        "set_motor_state", "强制下发控制指令",
        json{{"type", "object"}, {"properties", {{"speed", {{"type", "number"}}}, {"torque", {{"type", "number"}}}, {"direction", {{"type", "integer"}}}}}},
        [&mcp_bridge](const json& args) -> json {
            MotorControl cmd{args.value("speed", 0.0f), args.value("torque", 0.0f), args.value("direction", 1)};
            int target_id = mcp_bridge.lookup_node("DashboardUI", TYPE_MOTOR_CONTROL);
            if (target_id >= 0 && mcp_bridge.forward(target_id, TYPE_MOTOR_CONTROL, &cmd, sizeof(cmd))) {
                return {{"content", json::array({{{"type", "text"}, {"text", "指令已成功下发！"}}})}};
            }
            return {{"isError", true}, {"content", json::array({{{"type", "text"}, {"text", "发送失败"}}})}};
        }
    );

    std::thread bus_thread([&mcp_bridge]() { mcp_bridge.run(); });

    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            json req = json::parse(line);
            
            // 【健壮性补丁】：防止非标准 JSON 对象导致程序崩溃
            if (!req.is_object() || !req.contains("method")) continue;
            
            bool is_request = req.contains("id");
            std::string method = req["method"];
            if (!is_request) continue; 

            // 【健壮性补丁】：安全获取 id
            json res = {{"jsonrpc", "2.0"}, {"id", req.value("id", json(nullptr))}};
            
            if (method == "initialize") res["result"] = {{"protocolVersion", "2024-11-05"}, {"capabilities", {{"resources", json::object()}, {"tools", json::object()}}}, {"serverInfo", {{"name", "cxx-softbus-mcp"}, {"version", "1.0.0"}}}};
            else if (method == "ping") res["result"] = json::object(); 
            else if (method == "resources/list") res["result"] = mcp_bridge.mcp_list_resources();
            else if (method == "resources/read") res["result"] = mcp_bridge.mcp_read_resource(req["params"]["uri"].get<std::string>());
            else if (method == "tools/list") res["result"] = mcp_bridge.mcp_list_tools();
            else if (method == "tools/call") res["result"] = mcp_bridge.mcp_call_tool(req["params"]["name"].get<std::string>(), req["params"].value("arguments", json::object()));
            else res["error"] = {{"code", -32601}, {"message", "Method not found"}};
            
            std::cout << res.dump() << std::endl;
        } catch (const std::exception& e) {
            // 【健壮性补丁】：仅打印警告，绝不退出进程
            std::cerr << "[Warning] Invalid MCP message dropped: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[Warning] Unknown exception in MCP loop!" << std::endl;
        }
    }

    mcp_bridge.stop(); 
    if (bus_thread.joinable()) bus_thread.join(); 
    return 0;
}