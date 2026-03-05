#!/bin/bash

# ============================================================================
# Softbus IPC 极速启动脚本 (防 OOM 内存泄漏加固版)
# ============================================================================

BUILD_DIR="./build"

if [ ! -f "$BUILD_DIR/shm_registry" ]; then
    echo "❌ 错误: 找不到可执行文件。请先在 build 目录下执行 make 编译。"
    exit 1
fi

echo "🚀 正在清理历史遗留的系统资源..."

# 1. 暴力清剿可能逃逸的孤儿进程，防止它们锁死内存
killall -9 node_mcp 2>/dev/null
killall -9 npx 2>/dev/null

# 2. 清理共享内存文件
rm -f /dev/shm/shm_bus_region 2>/dev/null

echo "✅ 资源清理完毕，正在启动 Softbus 底层框架..."

cleanup() {
    echo ""
    echo "🛑 收到停止信号，正在安全剥离所有节点..."
    
    # 杀掉脚本衍生的后台进程
    kill $LOGGER_PID $SINK_PID $SOURCE_PID $REGISTRY_PID 2>/dev/null
    wait $LOGGER_PID $SINK_PID $SOURCE_PID $REGISTRY_PID 2>/dev/null
    
    # 【核心修复】：必须连带追杀另一个终端的大模型网关进程！
    killall -9 node_mcp 2>/dev/null
    
    # 【核心修复】：退出时必须彻底销毁共享内存文件，把 RAM 还给操作系统！
    rm -f /dev/shm/shm_bus_region 2>/dev/null
    
    echo "✅ 所有进程与 tmpfs 物理内存已彻底释放，你的 SSH 安全了！"
    exit 0
}
trap cleanup SIGINT SIGTERM

echo "   -> [1/5] 启动注册中心 (Registry)..."
$BUILD_DIR/shm_registry > /dev/null 2>&1 &
REGISTRY_PID=$!
sleep 0.5 

echo "   -> [2/5] 启动旁路监控 (Logger)..."
$BUILD_DIR/shm_logger > /dev/null 2>&1 &
LOGGER_PID=$!

echo "   -> [3/5] 启动节点A：温度传感器 (Source)..."
$BUILD_DIR/node_source > /dev/null 2>&1 &
SOURCE_PID=$!

echo "   -> [4/5] 启动节点B：AI控制器 (Normal)..."
$BUILD_DIR/node_normal &
NORMAL_PID=$!

echo "   -> [5/5] 启动节点C：仪表盘 (Sink)..."
$BUILD_DIR/node_sink &
SINK_PID=$!
sleep 0.5

echo "========================================================"
echo "🎉 软总线底座已全面就绪！"
echo "🧠 请在第二个终端执行以下命令唤醒 Inspector："
echo -e "npx @modelcontextprotocol/inspector $BUILD_DIR/node_mcp \033[0m"
echo "========================================================"

wait