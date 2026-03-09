#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <string_view>
#include <nlohmann/json.hpp>
#include "framework/softbus_node.h"

using json = nlohmann::json;

namespace shm_bus {

class MCPEngine {
public:
    MCPEngine(std::string_view name, std::string_view version);
    ~MCPEngine();

    // 阻塞式主循环，接管 std::cin/cout 通信
    void run_blocking();

    // ========================================================================
    // 资源绑定 (直接打通无锁缓存)
    // ========================================================================
    using ResourceParser = std::function<json(const void* raw_payload)>;
    
    template<typename T>
    void bind_cache_resource(std::string_view resource_name, uint32_t type_hash, std::function<json(const T*)> parser) {
        resource_bindings_[std::string("bus://types/") + std::string(resource_name) + "/latest"] = {
            std::string(resource_name),
            type_hash,
            [parser](const void* payload) -> json { return parser(static_cast<const T*>(payload)); }
        };
    }

    // ========================================================================
    // 工具绑定
    // ========================================================================
    using ToolHandler = std::function<json(const json& arguments)>;
    void register_tool(const json& schema, ToolHandler handler);

    // 暴露底层的总线节点指针，供回调函数(Tool)发送控制指令使用
    SourceNode* get_bus_node() { return &bus_node_; }

private:
    json handle_list_resources();
    json handle_read_resource(const std::string& uri);
    json handle_list_tools();
    json handle_call_tool(const std::string& name, const json& arguments);

    std::string server_name_;
    std::string server_version_;

    // 内置一个 SourceNode 纯用于向底层发送大模型的控制指令
    SourceNode bus_node_; 

    struct ResourceInfo {
        std::string name;
        uint32_t type_hash;
        ResourceParser parser;
    };
    std::unordered_map<std::string, ResourceInfo> resource_bindings_;

    struct ToolInfo {
        json schema;
        ToolHandler handler;
    };
    std::unordered_map<std::string, ToolInfo> tools_;
};

} // namespace shm_bus