#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <shared_mutex>
#include <string_view>
#include <nlohmann/json.hpp>
#include "framework/softbus_node.h"

using json = nlohmann::json;

namespace shm_bus {

class MCPResourceNode : public NormalNode {
public:
    MCPResourceNode() : NormalNode("MCPServerBridge", 0) {
        init_bus_handler();
    }

    // ========================================================================
    // Resources (被动状态投影)
    // ========================================================================
    using ParserFunc = std::function<json(const EventData*)>;
    
    void register_type_parser(uint32_t type_id, std::string_view resource_name, ParserFunc parser) {
        type_parsers_[type_id] = {std::string(resource_name), std::move(parser)};
    }

    json mcp_list_resources();
    json mcp_read_resource(std::string_view uri);

    // ========================================================================
    // Tools (主动工具调用)
    // ========================================================================
    using ToolHandler = std::function<json(const json& arguments)>;
    
    // 注册一个供 LLM 调用的工具
    void register_tool(std::string_view name, std::string_view description, const json& input_schema, ToolHandler handler) {
        tools_[std::string(name)] = {std::string(description), input_schema, std::move(handler)};
    }
    
    json mcp_list_tools();
    json mcp_call_tool(std::string_view name, const json& arguments);

private:
    void init_bus_handler();

    struct ParserInfo {
        std::string resource_name;
        ParserFunc parse;
    };
    std::unordered_map<uint32_t, ParserInfo> type_parsers_;
    std::unordered_map<std::string, json> shadow_cache_;
    mutable std::shared_mutex cache_mutex_; 

    struct ToolInfo {
        std::string description;
        json input_schema;
        ToolHandler handler;
    };
    std::unordered_map<std::string, ToolInfo> tools_;
};

} // namespace shm_bus