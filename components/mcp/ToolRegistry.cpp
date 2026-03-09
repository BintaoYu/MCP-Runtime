#include "ToolRegistry.hpp"
#include "../apps/business_types.h"

namespace shm_bus {

void ToolRegistry::register_all(MCPEngine& engine) {

    // 1. 获取底层网络拓扑
    engine.register_tool(
        ToolBuilder("get_bus_topology").description("获取当前底层的 Pub/Sub 路由拓扑表").build(),
        [&engine](const json& /*args*/) -> json {
            json routes = json::array();
            for (int msg_type = 0; msg_type < 256; ++msg_type) {
                std::vector<int> subscribers = engine.get_bus_node()->get_routes(msg_type);
                if (!subscribers.empty()) {
                    json subs_json = json::array();
                    for (int tid : subscribers) {
                        subs_json.push_back({
                            {"target_node_id", tid},
                            {"target_node_name", engine.get_bus_node()->get_node_name(tid)}
                        });
                    }
                    routes.push_back({{"msg_type", msg_type}, {"subscribers", subs_json}});
                }
            }
            if (routes.empty()) return {{"content", json::array({{{"type", "text"}, {"text", "当前路由表为空"}}})}};
            return {{"content", json::array({{{"type", "text"}, {"text", routes.dump(2)}}})}};
        }
    );

    // 2. 配置拓扑订阅
    engine.register_tool(
        ToolBuilder("update_bus_topology")
            .description("配置动态 Pub/Sub 路由表")
            .add_string("action", "操作: 'connect' 或 'disconnect'", true)
            .add_integer("msg_type", "数据类型ID", true)
            .add_string("target_node_name", "操作的目标节点名称", true)
            .build(),
        [&engine](const json& args) -> json {
            std::string action = args.value("action", "connect");
            int msg_type = args.value("msg_type", -1);
            std::string target_name = args.value("target_node_name", "");
            
            if (msg_type < 0 || msg_type > 255) return {{"isError", true}, {"content", json::array({{{"type", "text"}, {"text", "非法的 msg_type"}}})}};
            
            int target_id = engine.get_bus_node()->lookup_node(target_name, msg_type);
            if (target_id < 0) return {{"isError", true}, {"content", json::array({{{"type", "text"}, {"text", "路由失败：目标离线或类型不匹配"}}})}};

            if (action == "connect") {
                engine.get_bus_node()->add_route(msg_type, target_id);
                return {{"content", json::array({{{"type", "text"}, {"text", "已成功订阅 Topic"}}})}};
            } else {
                engine.get_bus_node()->remove_route(msg_type, target_id);
                return {{"content", json::array({{{"type", "text"}, {"text", "已成功退订 Topic"}}})}};
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
            
            int target_id = engine.get_bus_node()->lookup_node("DashboardUI", TYPE_MOTOR_CONTROL);
            if (target_id >= 0 && engine.get_bus_node()->emit(target_id, TYPE_MOTOR_CONTROL, &cmd, sizeof(cmd))) {
                return {{"content", json::array({{{"type", "text"}, {"text", "指令已成功下发！"}}})}};
            }
            return {{"isError", true}, {"content", json::array({{{"type", "text"}, {"text", "发送失败"}}})}};
        }
    );
}

} // namespace shm_bus