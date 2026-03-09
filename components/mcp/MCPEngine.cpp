#include "MCPEngine.hpp"
#include <iostream>
#include <csignal>

namespace shm_bus {

MCPEngine::MCPEngine(std::string_view name, std::string_view version)
    : server_name_(name), server_version_(version), bus_node_("MCPServerEngine") {}

MCPEngine::~MCPEngine() {
    bus_node_.stop();
}

void MCPEngine::run_blocking() {
    // 【底层保护】：彻底忽略 SIGPIPE，网页关闭不会导致进程崩溃
    std::signal(SIGPIPE, SIG_IGN);
    
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            json req = json::parse(line);
            if (!req.is_object() || !req.contains("method")) continue;
            
            bool is_request = req.contains("id");
            std::string method = req["method"];
            if (!is_request) continue; 

            json res = {{"jsonrpc", "2.0"}, {"id", req.value("id", json(nullptr))}};
            
            if (method == "initialize") {
                res["result"] = {
                    {"protocolVersion", "2024-11-05"},
                    {"capabilities", {{"resources", json::object()}, {"tools", json::object()}}},
                    {"serverInfo", {{"name", server_name_}, {"version", server_version_}}}
                };
            }
            else if (method == "ping") res["result"] = json::object(); 
            else if (method == "resources/list") res["result"] = handle_list_resources();
            else if (method == "resources/read") res["result"] = handle_read_resource(req["params"]["uri"].get<std::string>());
            else if (method == "tools/list") res["result"] = handle_list_tools();
            else if (method == "tools/call") res["result"] = handle_call_tool(req["params"]["name"].get<std::string>(), req["params"].value("arguments", json::object()));
            else res["error"] = {{"code", -32601}, {"message", "Method not found"}};
            
            std::cout << res.dump() << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[MCPEngine] Invalid message dropped: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[MCPEngine] Unknown exception in event loop" << std::endl;
        }
    }
}

json MCPEngine::handle_list_resources() {
    json resources = json::array();
    for (const auto& [uri, info] : resource_bindings_) {
        resources.push_back({
            {"uri", uri},
            {"name", "Real-time state of " + info.name},
            {"mimeType", "application/json"}
        });
    }
    return json{{"resources", resources}};
}

json MCPEngine::handle_read_resource(const std::string& uri) {
    auto it = resource_bindings_.find(uri);
    if (it == resource_bindings_.end()) {
        return json{{"contents", json::array({{{"uri", uri}, {"text", "{\"error\": \"Resource not found\"}"}}})}};
    }

    // 【核心亮点】：零等待！直接从底层的高并发无锁哈希表中拉取最新状态
    alignas(64) char buffer[256]; 
    size_t len = bus_node_.get_state(it->second.type_hash, buffer, sizeof(buffer));
    
    if (len > 0) {
        json parsed = it->second.parser(buffer);
        return json{{"contents", json::array({{{"uri", uri}, {"mimeType", "application/json"}, {"text", parsed.dump()}}})}};
    } else {
        return json{{"contents", json::array({{{"uri", uri}, {"mimeType", "application/json"}, {"text", "{\"status\": \"waiting_for_hardware\"}"}}})}};
    }
}

void MCPEngine::register_tool(const json& schema, ToolHandler handler) {
    if (schema.contains("name")) {
        tools_[schema["name"].get<std::string>()] = {schema, std::move(handler)};
    }
}

json MCPEngine::handle_list_tools() {
    json tool_list = json::array();
    for (const auto& [name, info] : tools_) {
        tool_list.push_back(info.schema);
    }
    return json{{"tools", tool_list}};
}

json MCPEngine::handle_call_tool(const std::string& name, const json& arguments) {
    auto it = tools_.find(name);
    if (it == tools_.end()) return json{{"isError", true}, {"content", json::array({{{"type", "text"}, {"text", "Tool not found"}}})}};
    try {
        return it->second.handler(arguments);
    } catch (const std::exception& e) {
        return json{{"isError", true}, {"content", json::array({{{"type", "text"}, {"text", std::string("Tool failed: ") + e.what()}}})}};
    }
}

} // namespace shm_bus