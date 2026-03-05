#include "MCPResourceNode.hpp"
#include <mutex>
#include <cstdio>    // 【新增】用于 popen
#include <array>     // 【新增】
#include <memory>    // 【新增】
#include <sstream>   // 【新增】用于字符串分割

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

// 【新增】核心实现：旁路读取 CSV 并解析为 JSON
json MCPResourceNode::get_latency_stats(int tail_lines) {
    // 调用系统命令获取 CSV 最后 N 行，丢弃标准错误输出
    std::string command = "tail -n " + std::to_string(tail_lines) + " p2p_latency_trace.csv 2>/dev/null";
    std::array<char, 128> buffer;
    std::string result;
    
    // C++17 RAII 风格的安全管道执行
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) {
        return json{{"error", "无法读取延迟数据，Logger 服务是否正在运行？"}};
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }

    json stats_array = json::array();
    std::istringstream iss(result);
    std::string line;
    
    // 简易的 CSV to JSON 解析器
    while (std::getline(iss, line)) {
        // 过滤空行和表头
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
            } catch (...) {
                // 忽略解析失败的脏数据行
            }
        }
    }
    return stats_array;
}

json MCPResourceNode::mcp_list_resources() {
    json resources = json::array();
    
    {
        std::shared_lock lock(cache_mutex_);
        for (const auto& [uri, json_data] : shadow_cache_) {
            resources.push_back({
                {"uri", uri},
                {"name", "Real-time state of " + uri},
                {"mimeType", "application/json"}
            });
        }
    } // 提前释放锁
    
    // 【新增】向大模型宣告：我这里有一个全局的诊断资源！
    resources.push_back({
        {"uri", "bus://diagnostics/latency_stats"},
        {"name", "System IPC Latency Statistics (Last 50 events)"},
        {"mimeType", "application/json"}
    });
    
    return json{{"resources", resources}};
}

json MCPResourceNode::mcp_read_resource(std::string_view uri) {
    // 【新增】拦截特殊的系统级诊断请求
    if (uri == "bus://diagnostics/latency_stats") {
        return json{
            {"contents", json::array({
                {
                    {"uri", std::string(uri)},
                    {"mimeType", "application/json"},
                    {"text", get_latency_stats(50).dump(2)} // 动态获取并格式化为优美的 JSON 文本
                }
            })}
        };
    }

    // 默认的业务缓存读取逻辑
    std::shared_lock lock(cache_mutex_); 
    auto it = shadow_cache_.find(std::string(uri)); 
    if (it != shadow_cache_.end()) {
        return json{
            {"contents", json::array({
                {
                    {"uri", std::string(uri)},
                    {"mimeType", "application/json"},
                    {"text", it->second.dump()} 
                }
            })}
        };
    }
    
    return json{{"error", "Resource not found on SoftBus"}};
}

} // namespace shm_bus