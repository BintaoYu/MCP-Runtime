#include "MCPResourceNode.hpp"
#include <mutex>

namespace shm_bus {

void MCPResourceNode::init_bus_handler() {
    this->set_handler([this](NormalNode*, const EventData* event) {
        auto it = type_parsers_.find(event->msg_type);
        if (it == type_parsers_.end()) return; 

        json latest_state = it->second.parse(event);
        std::string uri = "bus://types/" + it->second.resource_name + "/latest";

        {
            std::unique_lock lock(cache_mutex_); 
            shadow_cache_[uri] = std::move(latest_state);
        }
    });
}

json MCPResourceNode::mcp_list_resources() {
    json resources = json::array();
    std::shared_lock lock(cache_mutex_);
    for (const auto& [uri, json_data] : shadow_cache_) {
        resources.push_back({
            {"uri", uri},
            {"name", "Real-time state of " + uri},
            {"mimeType", "application/json"}
        });
    }
    return json{{"resources", resources}};
}

json MCPResourceNode::mcp_read_resource(std::string_view uri) {
    std::shared_lock lock(cache_mutex_); 
    auto it = shadow_cache_.find(std::string(uri)); 
    if (it != shadow_cache_.end()) {
        return json{{"contents", json::array({{{"uri", std::string(uri)}, {"mimeType", "application/json"}, {"text", it->second.dump()}}})}};
    }
    return json{{"error", "Resource not found on SoftBus"}};
}

// ----------------------------------------------------------------------------
// Tools 核心路由分发
// ----------------------------------------------------------------------------
json MCPResourceNode::mcp_list_tools() {
    json tool_list = json::array();
    for (const auto& [name, info] : tools_) {
        tool_list.push_back({
            {"name", name},
            {"description", info.description},
            {"inputSchema", info.input_schema}
        });
    }
    return json{{"tools", tool_list}};
}

json MCPResourceNode::mcp_call_tool(std::string_view name, const json& arguments) {
    auto it = tools_.find(std::string(name));
    if (it == tools_.end()) {
        return json{{"isError", true}, {"content", json::array({{{"type", "text"}, {"text", "Tool not found"}}})}};
    }
    try {
        return it->second.handler(arguments);
    } catch (const std::exception& e) {
        return json{{"isError", true}, {"content", json::array({{{"type", "text"}, {"text", std::string("Tool execution failed: ") + e.what()}}})}};
    }
}

} // namespace shm_bus