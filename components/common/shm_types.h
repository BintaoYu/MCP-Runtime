#pragma once
#include <cstdint>
#include <cstddef>
#include <string_view>

namespace shm_bus {

using offset_t = uint32_t;

inline constexpr offset_t NULL_OFFSET = 0xFFFFFFFF;
inline constexpr std::size_t BLOCK_SIZE = 256;

constexpr uint32_t hash_type(std::string_view str) {
    uint32_t hash = 2166136261u; // FNV_OFFSET_BASIS_32
    for (char c : str) {
        hash ^= static_cast<uint32_t>(c);
        hash *= 16777619u;       // FNV_PRIME_32
    }
    return hash;
}

#define TYPE_ID(type_name) shm_bus::hash_type(#type_name)

} // namespace shm_bus