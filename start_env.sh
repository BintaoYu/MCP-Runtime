#!/bin/bash

# ============================================================================
# Softbus IPC 极速启动脚本 (工业级实时流监控版 - 带 Docker Compose 风格彩色前缀)
# ============================================================================

BUILD_DIR="./build"
LOG_DIR="./logs"

if [ ! -f "$BUILD_DIR/shm_registry" ]; then
    echo "❌ 错误: 找不到可执行文件。请先在 build 目录下执行 make 编译。"
    exit 1
fi

echo "🚀 正在清理历史遗留的系统资源..."
killall -9 node_mcp shm_registry shm_logger node_source node_normal node_sink npx 2>/dev/null
rm -f /dev/shm/p2p_bus_shm 2>/dev/null
mkdir -p $LOG_DIR
rm -f $LOG_DIR/*.log

echo "✅ 资源清理完毕，正在启动 Softbus 底层框架..."
echo "--------------------------------------------------------"

cleanup() {
    echo -e "\n🛑 收到停止信号，正在安全剥离所有节点..."
    kill $REGISTRY_PID $LOGGER_PID $SOURCE_PID $NORMAL_PID $SINK_PID 2>/dev/null
    wait $REGISTRY_PID $LOGGER_PID $SOURCE_PID $NORMAL_PID $SINK_PID 2>/dev/null
    killall -9 node_mcp 2>/dev/null
    rm -f /dev/shm/p2p_bus_shm 2>/dev/null
    echo "✅ 所有进程与共享内存已彻底释放，你的 SSH 安全了！"
    exit 0
}
trap cleanup SIGINT SIGTERM

# ========================================================================
# 核心魔法：终极启动器
# 1. stdbuf -oL: 破解 C++ std::cout 遇到管道时的 4KB 阻塞缓冲，强制按行实时输出
# 2. >(...) : Bash 进程替换，保证外层的 $! 能拿到真实的 C++ 进程 PID，方便关闭
# 3. tee + awk: 将纯净原味日志写入文件，同时给终端屏幕加上炫酷的彩色进程名前缀
# ========================================================================
start_node() {
    local name=$1
    local color=$2
    shift 2
    local cmd="$@"
    local log="$LOG_DIR/${name}.log"
    
    stdbuf -o L -e L $cmd > >(tee "$log" | awk -v p="${color}[$name]\033[0m " '{print p $0; fflush()}') 2>&1 &
}

# 按顺序启动，并分配不同的高亮颜色
start_node "Registry" "\033[1;35m" $BUILD_DIR/shm_registry
REGISTRY_PID=$!
sleep 0.5 

start_node "Logger  " "\033[0;36m" $BUILD_DIR/shm_logger
LOGGER_PID=$!

start_node "Source  " "\033[1;32m" $BUILD_DIR/node_source
SOURCE_PID=$!

start_node "Normal  " "\033[1;33m" $BUILD_DIR/node_normal
NORMAL_PID=$!

start_node "Sink    " "\033[1;34m" $BUILD_DIR/node_sink
SINK_PID=$!
sleep 0.5

echo "========================================================"
echo -e "🎉 \033[1;32m软总线底座已全面就绪！系统日志正在实时滚动 👆\033[0m"
echo -e "🧠 请在第二个终端执行以下命令唤醒 Inspector："
echo -e "   \033[1;36mnpx @modelcontextprotocol/inspector $BUILD_DIR/node_mcp\033[0m"
echo "========================================================"

# 阻塞挂起主脚本，并等待 Ctrl+C 触发 cleanup
wait