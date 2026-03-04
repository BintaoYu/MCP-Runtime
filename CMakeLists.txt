# 声明要求的最低 CMake 版本
cmake_minimum_required(VERSION 3.10)

# 定义工程名称和支持的语言
project(ShmFramework VERSION 1.0 LANGUAGES CXX)

# ==============================================================================
# 全局编译选项配置
# ==============================================================================
# 强制使用 C++11 标准 (我们的无锁组件强依赖 C++11 的 memory_order)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 默认开启 Release 模式以获得极致性能 (-O3)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -Wall -Wextra")

# 引入多线程库 (必须)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# 设置全局头文件包含路径，这样各个模块里可以直接 #include "common/shm_types.h"
include_directories(${CMAKE_SOURCE_DIR}/components)

# ==============================================================================
# 1. 构建核心组件库 (静态库)
# ==============================================================================
# 将拆分出来的 .cpp 文件编译成名为 shm_allocator 的静态库 (libshm_allocator.a)
add_library(shm_allocator STATIC
    components/allocator/shm_allocator.cpp
)

# ==============================================================================
# 2. 构建独立的测试执行文件
# ==============================================================================
# 编译内存池的独立测试程序 (对应你重命名后的那个带有 main 的压测文件)
add_executable(test_allocator 
    components/allocator/test_allocator.cpp
)

# 将内存池静态库以及 pthread 多线程库链接到测试程序上
target_link_libraries(test_allocator 
    PRIVATE 
    shm_allocator 
    Threads::Threads
)


add_executable(test_mpsc components/mpsc_queue/test_mpsc.cpp)
target_link_libraries(test_mpsc PRIVATE Threads::Threads)

add_executable(shm_registry services/registry/main.cpp)
target_link_libraries(shm_registry 
    PRIVATE 
    shm_allocator 
    Threads::Threads 
    rt   # 链接 librt，这是使用 shm_open 必须的系统库
)

add_executable(shm_logger services/logger/main.cpp)
target_link_libraries(shm_logger PRIVATE Threads::Threads rt)

add_executable(node_source apps/node_source.cpp)
target_link_libraries(node_source PRIVATE shm_allocator Threads::Threads rt)

add_executable(node_sink apps/node_sink.cpp)
target_link_libraries(node_sink PRIVATE shm_allocator Threads::Threads rt)

add_executable(node_normal apps/node_normal.cpp)
target_link_libraries(node_normal PRIVATE shm_allocator Threads::Threads rt)

# ============================================================================
# 构建 MCP Server 网桥节点
# ============================================================================
# 由于 MCPResourceNode 的实现分离到了 .cpp 文件中，需要将其与主程序一起编译
add_executable(node_mcp 
    apps/node_mcp.cpp 
    components/mcp/MCPResourceNode.cpp
)

# 链接依赖：内存池、线程库、实时库 (rt)
target_link_libraries(node_mcp PRIVATE shm_allocator Threads::Threads rt)

# 包含 nlohmann_json 头文件路径 (如果通过 apt 安装，通常默认在系统路径下)
# 如果提示找不到 json.hpp，可以取消下面这行的注释并指定具体路径
# target_include_directories(node_mcp PRIVATE /usr/include/nlohmann)

# TODO: 之后我们每开发一个新模块，就在这里加上它的测试 target
# 例如：
# add_executable(test_mpsc components/mpsc_queue/test_mpsc.cpp)
# target_link_libraries(test_mpsc Threads::Threads)