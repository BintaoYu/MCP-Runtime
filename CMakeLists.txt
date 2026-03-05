cmake_minimum_required(VERSION 3.10)
project(softbus)

# ============================================================================
# 强制开启 C++17 标准
# ============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS False)

# 编译选项
add_compile_options(-Wall -Wextra -O3 -pthread)

# 【修复点】：显式查找系统的线程库，这样 CMake 才能识别 Threads::Threads
find_package(Threads REQUIRED)

# 包含目录
include_directories(${CMAKE_SOURCE_DIR}/components)

# 静态库：内存池
add_library(shm_allocator STATIC 
    components/allocator/shm_allocator.cpp
)

# 测试程序
add_executable(test_allocator components/allocator/test_allocator.cpp)
target_link_libraries(test_allocator PRIVATE shm_allocator Threads::Threads rt)

add_executable(test_mpsc components/mpsc_queue/test_mpsc.cpp)
target_link_libraries(test_mpsc PRIVATE shm_allocator Threads::Threads rt)

# 核心服务
add_executable(shm_registry services/registry/main.cpp)
target_link_libraries(shm_registry PRIVATE shm_allocator Threads::Threads rt)

add_executable(shm_logger services/logger/main.cpp)
target_link_libraries(shm_logger PRIVATE Threads::Threads rt)

# 业务节点应用
add_executable(node_source apps/node_source.cpp)
target_link_libraries(node_source PRIVATE shm_allocator Threads::Threads rt)

add_executable(node_sink apps/node_sink.cpp)
target_link_libraries(node_sink PRIVATE shm_allocator Threads::Threads rt)

add_executable(node_normal apps/node_normal.cpp)
target_link_libraries(node_normal PRIVATE shm_allocator Threads::Threads rt)

# ============================================================================
# 构建 MCP Server 网桥节点
# ============================================================================
add_executable(node_mcp 
    apps/node_mcp.cpp 
    components/mcp/MCPResourceNode.cpp
)
target_link_libraries(node_mcp PRIVATE shm_allocator Threads::Threads rt)