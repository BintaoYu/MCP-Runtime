#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <shared_mutex>
#include <nlohmann/json.hpp> // 假设使用大名鼎鼎的 nlohmann/json
#include "framework/softbus_node.h"

using json = nlohmann::json;

namespace shm_bus {

class MCPResourceNode : public NormalNode {
public:
    // 构造函数：命名为 MCPServerBridge，监听所有的事件 (假设 0 代表监听通配符)
    MCPResourceNode() : NormalNode("MCPServerBridge", 0) {
        init_bus_handler();
    }

    // ========================================================================
    // 类型解析器注册：教节点如何把 C++ 二进制内存块翻译成 JSON
    // ========================================================================
    using ParserFunc = std::function<json(const EventData*)>;
    
    void register_type_parser(uint32_t type_id, const std::string& resource_name, ParserFunc parser) {
        type_parsers_[type_id] = {resource_name, std::move(parser)};
    }

    // ========================================================================
    // MCP 协议核心接口 (供外部 HTTP/Stdio 框架异步调用)
    // ========================================================================
    
    // 对应 MCP: resources/list
    json mcp_list_resources();

    // 对应 MCP: resources/read
    json mcp_read_resource(const std::string& uri);

private:
    // 软总线的高频回调监听
    void init_bus_handler();

    struct ParserInfo {
        std::string resource_name;
        ParserFunc parse;
    };

    // 解析器路由表：TypeID -> 解析逻辑
    std::unordered_map<uint32_t, ParserInfo> type_parsers_;

    // ========================================================================
    // 核心态投影 (State Shadow Cache)
    // URI -> 最新状态的 JSON 快照
    // ========================================================================
    std::unordered_map<std::string, json> shadow_cache_;
    
    // 读写锁：保护影子缓存。
    // SoftBus 回调高频获取 Write 锁；MCP Server 低频获取 Read 锁。
    mutable std::shared_mutex cache_mutex_; 
};

} // namespace shm_bus