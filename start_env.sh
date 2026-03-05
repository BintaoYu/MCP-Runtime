#!/bin/bash

# ============================================================================
# Softbus IPC 极速启动脚本
# ============================================================================

BUILD_DIR="./build"

# 检查是否编译完成
if [ ! -f "$BUILD_DIR/shm_registry" ]; then
    echo "❌ 错误: 找不到可执行文件。请先在 build 目录下执行 make 编译。"
    exit 1
fi

echo "🚀 正在启动 Softbus 底层框架..."

# 1. 清理可能残留的 IPC 共享内存文件 (防呆设计)
rm -f /dev/shm/shm_bus_region 2>/dev/null

# 2. 定义清理函数，拦截 Ctrl+C 信号 (SIGINT)
cleanup() {
    echo ""
    echo "🛑 收到停止信号，正在安全关闭所有节点..."
    # 优雅杀死所有后台衍生的进程
    kill $LOGGER_PID $SINK_PID $SOURCE_PID $REGISTRY_PID 2>/dev/null
    wait $LOGGER_PID $SINK_PID $SOURCE_PID $REGISTRY_PID 2>/dev/null
    echo "✅ 所有 Softbus 节点已安全退出。"
    exit 0
}
trap cleanup SIGINT SIGTERM

# 3. 按照严格依赖顺序启动底层组件，并放入后台 (&)
echo "   -> [1/4] 启动注册中心 (Registry)..."
$BUILD_DIR/shm_registry > /dev/null 2>&1 &
REGISTRY_PID=$!
sleep 0.5 # 给予 500ms 让共享内存完成 mmap 初始化

echo "   -> [2/4] 启动旁路监控 (Logger)..."
$BUILD_DIR/shm_logger > /dev/null 2>&1 &
LOGGER_PID=$!

echo "   -> [3/4] 启动电机控制器 (Sink)..."
$BUILD_DIR/node_sink > /dev/null 2>&1 &
SINK_PID=$!

echo "   -> [4/4] 启动温度传感器 (Source)..."
$BUILD_DIR/node_source > /dev/null 2>&1 &
SOURCE_PID=$!

echo "========================================================"
echo "🎉 软总线底座已全面就绪！100ns 极速链路运转中。"
echo "📝 旁路监控文件 p2p_latency_trace.csv 正在后台持续生成。"
echo "========================================================"
echo ""
echo "🧠 现在，你可以安全地将当前终端交给大模型 MCP 网关了。"
echo "请直接复制并执行以下命令唤醒 Inspector："
echo ""
echo -e "\033[1;32m npx @modelcontextprotocol/inspector $BUILD_DIR/node_mcp \033[0m"
echo ""
echo "调试结束后，在此终端按下 [Ctrl+C] 即可一键销毁所有底层进程。"

# 挂起当前脚本，等待 Ctrl+C 触发 cleanup
wait