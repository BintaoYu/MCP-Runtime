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

    using ParserFunc = std::function<json(const EventData*)>;
    
    void register_type_parser(uint32_t type_id, std::string_view resource_name, ParserFunc parser) {
        type_parsers_[type_id] = {std::string(resource_name), std::move(parser)};
    }

    json mcp_list_resources();
    json mcp_read_resource(std::string_view uri);

private:
    void init_bus_handler();
    
    // 【新增】读取 Logger 产生的 CSV 尾部数据，转换为 JSON 数组
    json get_latency_stats(int tail_lines = 50);

    struct ParserInfo {
        std::string resource_name;
        ParserFunc parse;
    };

    std::unordered_map<uint32_t, ParserInfo> type_parsers_;
    std::unordered_map<std::string, json> shadow_cache_;
    
    mutable std::shared_mutex cache_mutex_; 
};

} // namespace shm_bus