#include "MCPResourceNode.hpp"
#include <iostream>

namespace shm_bus {

void MCPResourceNode::init_bus_handler() {
    // 注入 Soft Bus 的高频回调 (亚微秒级触发)
    this->set_handler([this](NormalNode* self, const EventData* event) {
        
        // 1. 查表：当前收到的事件，有没有注册对应的 JSON 解析器？
        auto it = type_parsers_.find(event->msg_type);
        if (it == type_parsers_.end()) {
            return; // 遇到不关心的类型，直接极速丢弃，不浪费哪怕 1 纳秒
        }

        // 2. 语义转换：C++ 裸内存 -> JSON 对象
        json latest_state = it->second.parse(event);
        
        // 3. 构建工业风格的 URI，例如：bus://types/MotorControl/latest
        std::string uri = "bus://types/" + it->second.resource_name + "/latest";

        // 4. 更新影子缓存 (加写锁)
        {
            std::unique_lock<std::shared_mutex> lock(cache_mutex_);
            shadow_cache_[uri] = std::move(latest_state);
        }
    });
}

// 供 MCP Server 调用的 List 接口
json MCPResourceNode::mcp_list_resources() {
    json resources = json::array();
    
    // 加读锁，防止与 SoftBus 高频写入冲突
    std::shared_lock<std::shared_mutex> lock(cache_mutex_);
    for (const auto& pair : shadow_cache_) {
        resources.push_back({
            {"uri", pair.first},
            {"name", "Real-time state of " + pair.first},
            {"mimeType", "application/json"}
        });
    }
    
    return json{{"resources", resources}};
}

// 供 MCP Server 调用的 Read 接口
json MCPResourceNode::mcp_read_resource(const std::string& uri) {
    std::shared_lock<std::shared_mutex> lock(cache_mutex_);
    
    auto it = shadow_cache_.find(uri);
    if (it != shadow_cache_.end()) {
        return json{
            {"contents", json::array({
                {
                    {"uri", uri},
                    {"mimeType", "application/json"},
                    {"text", it->second.dump()} // 序列化为字符串返回
                }
            })}
        };
    }
    
    return json{{"error", "Resource not found on SoftBus"}};
}

} // namespace shm_bus