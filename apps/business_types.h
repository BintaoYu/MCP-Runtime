#pragma once
#include <cstdint>

namespace shm_bus {

// 【架构升级】：显式定义数据字典，大模型将根据这些 ID 进行空中连线
constexpr uint32_t TYPE_SENSOR_DATA = 1;
constexpr uint32_t TYPE_MOTOR_CONTROL = 2;

// 类型 1：原始环境数据
struct SensorData {
    float temperature;
    float humidity;
};

// 类型 2：执行机构控制流
struct MotorControl {
    float speed;
    float torque;
    int direction;
};

} // namespace shm_bus