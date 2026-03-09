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
    // 利用 nlohmann/json 的特性，直接将结构体解引用并转化为 json
    template<typename T>
    void bind_auto_resource(std::string_view resource_name) {
        // 【核心改变】：现在用具体的实例名（比如 "LivingRoomSensor"）来算 Hash，而不是用类型名！
        uint32_t key_hash = shm_bus::hash_type(resource_name); 
        
        resource_bindings_[std::string("bus://sensors/") + std::string(resource_name) + "/latest"] = {
            std::string(resource_name),
            key_hash,
            [](const void* payload) -> json { 
                // 但二进制强转时，依然使用 T (比如 SensorData)
                return json(*(static_cast<const T*>(payload))); 
            }
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
// 绑定宏：把类型名转为字符串传进模板
#define REG_MCP_CACHE_NAMED(engine, resource_name, Type) engine.bind_auto_resource<Type>(resource_name)
#define REG_MCP_CACHE(engine, Type) engine.bind_auto_resource<Type>(#Type)
} // namespace shm_bus
