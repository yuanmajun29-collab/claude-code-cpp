#!/bin/bash
# Claude Code C++ - 环境设置脚本
# 用于安装开发所需的所有依赖

set -e

echo "🦞 Claude Code C++ - 环境设置"
echo "================================"

# 检测操作系统
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    OS="ubuntu"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    OS="macos"
else
    echo "❌ 不支持的操作系统: $OSTYPE"
    exit 1
fi

echo "📋 检测到操作系统: $OS"

# 更新包管理器
echo ""
echo "📦 更新包管理器..."
if [ "$OS" == "ubuntu" ]; then
    sudo apt-get update
elif [ "$OS" == "macos" ]; then
    brew update
fi

# 安装 CMake
echo ""
echo "🔧 安装 CMake..."
if ! command -v cmake &> /dev/null; then
    if [ "$OS" == "ubuntu" ]; then
        sudo apt-get install -y cmake
    elif [ "$OS" == "macos" ]; then
        brew install cmake
    fi
    echo "✅ CMake 安装完成"
else
    CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
    echo "✅ CMake 已安装: $CMAKE_VERSION"
fi

# 检查 CMake 版本
CMAKE_MAJOR=$(cmake --version | head -n1 | cut -d' ' -f3 | cut -d'.' -f1)
if [ "$CMAKE_MAJOR" -lt 3 ]; then
    echo "❌ CMake 版本过低，需要 3.22 或更高版本"
    exit 1
fi
echo "✅ CMake 版本检查通过"

# 安装开发工具
echo ""
echo "🛠️  安装开发工具..."
if [ "$OS" == "ubuntu" ]; then
    sudo apt-get install -y \
        build-essential \
        git \
        wget \
        curl
elif [ "$OS" == "macos" ]; then
    brew install git wget curl
fi
echo "✅ 开发工具安装完成"

# 安装 C++ 库依赖
echo ""
echo "📚 安装 C++ 库依赖..."
if [ "$OS" == "ubuntu" ]; then
    sudo apt-get install -y \
        libcurl4-openssl-dev \
        libssl-dev \
        zlib1g-dev \
        pkg-config

    # nlohmann/json 可通过包管理器安装，但项目使用 FetchContent
    # 如果想使用系统包，取消下面这行的注释：
    # sudo apt-get install -y nlohmann-json3-dev

    echo "✅ 库依赖安装完成"

elif [ "$OS" == "macos" ]; then
    brew install openssl curl pkg-config
    echo "✅ 库依赖安装完成"
fi

# 安装 Google Test（可选，用于测试）
echo ""
read -p "是否安装 Google Test（用于运行单元测试）？[y/N] " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    if [ "$OS" == "ubuntu" ]; then
        sudo apt-get install -y libgtest-dev
    elif [ "$OS" == "macos" ]; then
        brew install googletest
    fi
    echo "✅ Google Test 安装完成"
fi

# 验证安装
echo ""
echo "🔍 验证安装..."

check_command() {
    if command -v $1 &> /dev/null; then
        echo "  ✅ $1: $($1 --version 2>&1 | head -n1 || echo "已安装")"
    else
        echo "  ❌ $1: 未安装"
        return 1
    fi
}

check_command cmake
check_command g++
check_command git
check_command curl
check_command pkg-config

# 检查库
echo ""
echo "📚 检查库..."

check_lib() {
    if [ "$OS" == "ubuntu" ]; then
        if pkg-config --exists $1; then
            echo "  ✅ $1: $(pkg-config --modversion $1)"
        else
            echo "  ⚠️  $1: 未找到（可能通过 FetchContent 自动下载）"
        fi
    elif [ "$OS" == "macos" ]; then
        echo "  ℹ️  macOS 库检查跳过"
    fi
}

check_lib libcurl
check_lib openssl

# 创建构建目录
echo ""
echo "🏗️  准备构建环境..."
mkdir -p build
echo "✅ 构建目录已创建: $(pwd)/build"

# 完成
echo ""
echo "================================"
echo "✅ 环境设置完成！"
echo ""
echo "下一步："
echo "  1. 配置项目: cd build && cmake .."
echo "  2. 编译项目: cmake --build . -j\$(nproc)"
echo "  3. 运行测试: ctest --output-on-failure"
echo ""
echo "详细文档请参考: BUILD_GUIDE.md"
