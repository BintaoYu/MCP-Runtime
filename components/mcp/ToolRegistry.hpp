#pragma once

#include "MCPEngine.hpp"

namespace shm_bus {

class ToolBuilder {
public:
    ToolBuilder(std::string name) {
        schema_ = {
            {"name", name},
            {"description", ""},
            {"inputSchema", {
                {"type", "object"},
                {"properties", json::object()}
            }}
        };
        required_ = json::array();
    }

    ToolBuilder& description(std::string desc) {
        schema_["description"] = desc;
        return *this;
    }

    ToolBuilder& add_number(std::string prop, std::string desc, bool required = false) {
        schema_["inputSchema"]["properties"][prop] = {{"type", "number"}, {"description", desc}};
        if (required) required_.push_back(prop);
        return *this;
    }

    ToolBuilder& add_integer(std::string prop, std::string desc, bool required = false) {
        schema_["inputSchema"]["properties"][prop] = {{"type", "integer"}, {"description", desc}};
        if (required) required_.push_back(prop);
        return *this;
    }

    ToolBuilder& add_string(std::string prop, std::string desc, bool required = false) {
        schema_["inputSchema"]["properties"][prop] = {{"type", "string"}, {"description", desc}};
        if (required) required_.push_back(prop);
        return *this;
    }

    json build() {
        if (!required_.empty()) {
            schema_["inputSchema"]["required"] = required_;
        }
        return schema_;
    }

private:
    json schema_;
    json required_;
};

// 工具注册中心门面
class ToolRegistry {
public:
    static void register_all(MCPEngine& engine);
};

} // namespace shm_bus