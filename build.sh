#!/bin/bash

# --- 配置区域 ---
# 如果系统环境变量没设 VCPKG_ROOT，请在这里填入绝对路径
export VCPKG_ROOT="/home/a/vcpkg" 

# 检查 VCPKG_ROOT 是否设置
if [ -z "$VCPKG_ROOT" ]; then
    echo "❌ 错误: VCPKG_ROOT 环境变量未设置！"
    echo "请执行 export VCPKG_ROOT=/path/to/vcpkg 或在脚本中指定。"
    exit 1
fi

echo "ℹ️  VCPKG_ROOT 路径为: $VCPKG_ROOT"

BUILD_DIR="build"

# 如果参数是 -f，则强制删除 build 目录重新构建
if [ "$1" == "-f" ]; then
    echo "🧹 收到 -f 参数，正在清理旧的构建目录..."
    rm -rf "$BUILD_DIR"
fi

# 创建目录
if [ ! -d "$BUILD_DIR" ]; then
    mkdir "$BUILD_DIR"
fi

cd "$BUILD_DIR"

# --- CMake 配置 ---
# 只有当 Makefile 不存在时，才运行 CMake
if [ ! -f "Makefile" ]; then
    echo "🔧 正在执行 CMake 配置..."
    
    cmake .. \
        -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
        -DCMAKE_BUILD_TYPE=Debug
    
    # 检查 CMake 是否成功，如果失败直接退出
    if [ $? -ne 0 ]; then
        echo "❌ CMake 配置失败！请检查报错信息。"
        # 失败时删除 Makefile 避免下次误判
        rm -f Makefile
        exit 1
    fi
else
    echo "⏩ Makefile 已存在，跳过 CMake 配置..."
fi

# --- 编译 ---
echo "🔨 开始编译 (使用 $(nproc) 线程)..."
make -j$(nproc)

# 检查 make 是否成功
if [ $? -eq 0 ]; then
    echo "✅ 编译成功！正在运行 ./test ..."
    echo "-------------------------------------"
    ./server_main
    # ./test
else
    echo "❌ 编译失败。"
    exit 1
fi