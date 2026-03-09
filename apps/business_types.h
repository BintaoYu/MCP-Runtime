#pragma once
#include <cstdint>
#include <nlohmann/json.hpp>
namespace shm_bus {

// 【架构升级】：显式定义数据字典，大模型将根据这些 ID 进行空中连线
constexpr uint32_t TYPE_SENSOR_DATA = 1;
constexpr uint32_t TYPE_MOTOR_CONTROL = 2;

// 类型 1：原始环境数据
struct SensorData {
    float temperature;
    float humidity;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SensorData, temperature, humidity) //自动支持转化为 JSON


// 类型 2：执行机构控制流
struct MotorControl {
    float speed;
    float torque;
    int direction;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MotorControl, speed, torque, direction) //自动支持转化为 JSON


} // namespace shm_bus