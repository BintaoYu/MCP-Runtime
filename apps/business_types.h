#pragma once
#include <cstdint>

struct MotorControl {
    float speed;
    float torque;
    int direction;
};

struct TemperatureData {
    float celsius;
    uint32_t sensor_id;
};