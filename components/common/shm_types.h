#pragma once
#include <cstdint>
#include <cstddef>

namespace shm_bus {

// 定义偏移量类型，替代传统的 void* 指针，解决跨容器地址异构问题
using offset_t = uint32_t;

// 定义一个特殊的常量，代表空指针
constexpr offset_t NULL_OFFSET = 0xFFFFFFFF;

// 定义统一的内存块大小 (256 字节)
constexpr std::size_t BLOCK_SIZE = 256;

// 隐藏的递归实现函数
constexpr uint32_t hash_type_impl(const char* str, uint32_t hash) {
    return (*str == '\0') 
        ? hash 
        : hash_type_impl(str + 1, (hash ^ static_cast<uint32_t>(*str)) * 16777619u);
}

// 对外暴露的接口
constexpr uint32_t hash_type(const char* str) {
    return hash_type_impl(str, 2166136261u); // 传入 FNV-1a 的初始偏移量
}

// 辅助宏
#define TYPE_ID(type_name) shm_bus::hash_type(#type_name)

} // namespace shm_bus