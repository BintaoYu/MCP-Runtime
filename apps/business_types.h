// apps/business_types.h
#pragma once
#include <cstdint>

// 业务协议 1：电机控制指令
struct MotorControl {
    float speed;
    float torque;
    int direction;
};

// 业务协议 2：传感器数据
struct TemperatureData {
    float celsius;
    uint32_t sensor_id;
};