#include "ToolRegistry.hpp"
#include "../apps/business_types.h"

namespace shm_bus {

void ToolRegistry::register_all(MCPEngine& engine) {

    // 1. 获取底层网络拓扑 (🔥已升级为 3D SDN 视角)
    engine.register_tool(
        ToolBuilder("get_bus_topology").description("获取当前底层的 SDN 路由流表映射，查看具体的 点对点(Source->Type->Dest) 连线状态").build(),
        [&engine](const json& /*args*/) -> json {
            json routes = json::array();
            
            // 遍历所有可能的源节点
            for (int src_id = 0; src_id < MAX_NODES; ++src_id) {
                std::string src_name = engine.get_bus_node()->get_node_name(src_id);
                if (src_name == "Unknown") continue; // 跳过未注册的空槽位，极致优化

                // 遍历该源节点下的所有可能的数据类型槽位
                for (int msg_type = 0; msg_type < 256; ++msg_type) {
                    std::vector<int> subscribers = engine.get_bus_node()->get_routes(src_id, msg_type);
                    if (!subscribers.empty()) {
                        json subs_json = json::array();
                        for (int dst_id : subscribers) {
                            subs_json.push_back({
                                {"target_node_id", dst_id},
                                {"target_node_name", engine.get_bus_node()->get_node_name(dst_id)}
                            });
                        }
                        routes.push_back({
                            {"source_node_id", src_id},
                            {"source_node_name", src_name},
                            {"msg_type_hash_mod", msg_type},
                            {"subscribers", subs_json}
                        });
                    }
                }
            }
            if (routes.empty()) return {{"content", json::array({{{"type", "text"}, {"text", "当前 SDN 流表为空，没有任何连线。"}}})}};
            return {{"content", json::array({{{"type", "text"}, {"text", routes.dump(2)}}})}};
        }
    );

    // 2. 配置拓扑订阅 (已升级 SDN 模式)
    engine.register_tool(
        ToolBuilder("update_bus_topology")
            .description("配置动态 SDN 流表。明确将【特定源节点】的【特定类型数据】发送给【目标节点】。")
            .add_string("action", "操作: 'connect'(接通连线) 或 'disconnect'(断开连线)", true)
            .add_string("source_node_name", "发送端的节点名称 (例如: TempSensor)", true)
            .add_string("type_name", "数据流的类型名称 (例如: SensorData, MotorControl)", true)
            .add_string("target_node_name", "接收端的节点名称 (例如: AutomatedController)", true)
            .build(),
        [&engine](const json& args) -> json {
            std::string action = args.value("action", "connect");
            std::string source_name = args.value("source_node_name", "");
            std::string type_name = args.value("type_name", "");
            std::string target_name = args.value("target_node_name", "");
            
            if (source_name.empty() || type_name.empty() || target_name.empty()) {
                return {{"isError", true}, {"content", json::array({{{"type", "text"}, {"text", "源节点、目标节点或类型名不能为空"}}})}};
            }
            
            uint32_t msg_type = shm_bus::hash_type(type_name);
            
            int src_id = engine.get_bus_node()->lookup_node(source_name, 0);
            if (src_id < 0) return {{"isError", true}, {"content", json::array({{{"type", "text"}, {"text", "失败：源节点 [" + source_name + "] 离线！"}}})}};

            int dst_id = engine.get_bus_node()->lookup_node(target_name, msg_type);
            if (dst_id < 0) return {{"isError", true}, {"content", json::array({{{"type", "text"}, {"text", "失败：目标节点 [" + target_name + "] 离线或类型不匹配！"}}})}};

            if (action == "connect") {
                engine.get_bus_node()->add_route(src_id, msg_type, dst_id);
                return {{"content", json::array({{{"type", "text"}, {"text", "🔗 流表已下发！[" + source_name + "] --(" + type_name + ")--> [" + target_name + "]"}}})}};
            } else {
                engine.get_bus_node()->remove_route(src_id, msg_type, dst_id);
                return {{"content", json::array({{{"type", "text"}, {"text", "✂️ 流表已移除！[" + source_name + "] -X-(" + type_name + ")-X-> [" + target_name + "]"}}})}};
            }
        }
    );

    // 3. AI 直接下发控制算子
    engine.register_tool(
        ToolBuilder("set_motor_state")
            .description("强制下发控制指令")
            .add_number("speed", "设定转速", true)
            .add_number("torque", "设定扭矩", true)
            .add_integer("direction", "转向(1或-1)", true)
            .build(),
        [&engine](const json& args) -> json {
            MotorControl cmd{args.value("speed", 0.0f), args.value("torque", 0.0f), args.value("direction", 1)};
            
            int target_id = engine.get_bus_node()->lookup_node("AutomatedController", TYPE_MOTOR_CONTROL);
            if (target_id >= 0 && engine.get_bus_node()->emit(target_id, TYPE_MOTOR_CONTROL, &cmd, sizeof(cmd))) {
                return {{"content", json::array({{{"type", "text"}, {"text", "指令已成功下发给 AutomatedController！"}}})}};
            }
            return {{"isError", true}, {"content", json::array({{{"type", "text"}, {"text", "发送失败，目标控制器可能不在线"}}})}};
        }
    );
    // 4. 直接读取最新传感器数据（利用底层 O(1) 无锁缓存）
    engine.register_tool(
        ToolBuilder("read_sensor_now").description("立刻读取当前传感器的最新温度").build(),
        [&engine](const json& args) -> json {
            SensorData data;
            // 直接从底层 O(1) 无锁缓存中捞取最新一帧
            if (engine.get_bus_node()->get_state(shm_bus::hash_type("SensorData"), &data, sizeof(data)) > 0) {
                return {{"content", json::array({{{"type", "text"}, {"text", "当前温度: " + std::to_string(data.temperature)}}})}};
            }
            return {{"isError", true}, {"content", json::array({{{"type", "text"}, {"text", "暂无数据"}}})}};
        }
    );
}

} // namespace shm_bus