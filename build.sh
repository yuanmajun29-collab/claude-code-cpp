#!/bin/bash
# build.sh - Claude Code C++ 快速构建脚本

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 配置
BUILD_TYPE="${BUILD_TYPE:-Release}"
RUN_TESTS="${RUN_TESTS:-1}"
NPROC="${NPROC:-$(nproc)}"

# 帮助信息
if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    echo "🦞 Claude Code C++ - 快速构建脚本"
    echo ""
    echo "用法: ./build.sh [选项]"
    echo ""
    echo "选项:"
    echo "  -h, --help          显示帮助信息"
    echo "  -d, --debug         Debug 模式构建"
    echo "  -r, --release       Release 模式构建（默认）"
    echo "  -t, --no-tests      跳过测试"
    echo "  -c, --clean         清理构建目录"
    echo "  -j, --jobs N        使用 N 个并行作业"
    echo "  --install            安装到系统"
    echo ""
    echo "环境变量:"
    echo "  BUILD_TYPE          Debug 或 Release（默认: Release）"
    echo "  RUN_TESTS          1=运行测试, 0=跳过测试（默认: 1）"
    echo "  NPROC              并行作业数（默认: 所有核心）"
    echo ""
    echo "示例:"
    echo "  ./build.sh                      # Release 模式 + 测试"
    echo "  ./build.sh -d                   # Debug 模式 + 测试"
    echo "  ./build.sh -d -t                # Debug 模式，不运行测试"
    echo "  BUILD_TYPE=Debug ./build.sh      # Debug 模式"
    echo "  ./build.sh -j 4                 # 使用 4 个并行作业"
    exit 0
fi

# 解析参数
CLEAN=0
INSTALL=0
while [[ $# -gt 0 ]]; do
    case $1 in
        -d|--debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        -r|--release)
            BUILD_TYPE="Release"
            shift
            ;;
        -t|--no-tests)
            RUN_TESTS=0
            shift
            ;;
        -c|--clean)
            CLEAN=1
            shift
            ;;
        -j|--jobs)
            NPROC="$2"
            shift 2
            ;;
        --install)
            INSTALL=1
            shift
            ;;
        *)
            echo -e "${RED}❌ 未知选项: $1${NC}"
            exit 1
            ;;
    esac
done

echo -e "${BLUE}🦞 Claude Code C++ - 快速构建${NC}"
echo "================================"
echo -e "构建类型: ${GREEN}${BUILD_TYPE}${NC}"
echo -e "并行作业: ${GREEN}${NPROC}${NC}"
echo -e "运行测试: ${GREEN}$([ "$RUN_TESTS" = "1" ] && echo "是" || echo "否")${NC}"
echo ""

# 检查环境
echo -e "${BLUE}🔍 检查环境...${NC}"

if ! command -v cmake &> /dev/null; then
    echo -e "${RED}❌ CMake 未安装${NC}"
    echo "请运行: ./scripts/setup-env.sh"
    exit 1
fi

if ! command -v g++ &> /dev/null; then
    echo -e "${RED}❌ g++ 未安装${NC}"
    exit 1
fi

CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
echo -e "CMake 版本: ${GREEN}${CMAKE_VERSION}${NC}"

# 清理构建目录
if [ "$CLEAN" = "1" ]; then
    echo -e "${YELLOW}🧹 清理构建目录...${NC}"
    rm -rf build
fi

# 创建构建目录
echo -e "${BLUE}🏗️  准备构建环境...${NC}"
mkdir -p build
cd build

# 配置 CMake
echo -e "${BLUE}⚙️  配置 CMake...${NC}"
if [ "$BUILD_TYPE" = "Debug" ]; then
    TESTS_ARG="-DBUILD_TESTS=ON"
else
    TESTS_ARG="-DBUILD_TESTS=ON"
fi

cmake .. \
    -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
    ${TESTS_ARG} \
    -DENABLE_MCP=ON \
    -DENABLE_LSP=ON \
    -DENABLE_AGENT=ON \
    -DENABLE_BRIDGE=ON \
    -DENABLE_PLUGINS=ON \
    -DENABLE_SANDBOX=ON

if [ $? -ne 0 ]; then
    echo -e "${RED}❌ CMake 配置失败${NC}"
    exit 1
fi

echo -e "${GREEN}✅ CMake 配置完成${NC}"

# 编译
echo -e "${BLUE}🔨 编译项目...${NC}"
cmake --build . -j${NPROC}

if [ $? -ne 0 ]; then
    echo -e "${RED}❌ 编译失败${NC}"
    exit 1
fi

echo -e "${GREEN}✅ 编译成功${NC}"

# 运行测试
if [ "$RUN_TESTS" = "1" ]; then
    echo -e "${BLUE}🧪 运行测试...${NC}"
    ctest --output-on-failure

    if [ $? -ne 0 ]; then
        echo -e "${YELLOW}⚠️  部分测试失败${NC}"
        echo -e "${YELLOW}建议: 运行 ${GREEN}ctest --verbose${NC} 查看详细输出"
        exit 1
    fi

    echo -e "${GREEN}✅ 所有测试通过${NC}"
fi

# 安装
if [ "$INSTALL" = "1" ]; then
    echo -e "${BLUE}📦 安装...${NC}"
    sudo cmake --install .
    echo -e "${GREEN}✅ 安装完成${NC}"
fi

# 完成
echo ""
echo "================================"
echo -e "${GREEN}✅ 构建完成！${NC}"
echo ""
echo -e "可执行文件: ${GREEN}$(pwd)/claude-code${NC}"

if [ "$INSTALL" = "1" ]; then
    echo -e "安装位置: ${GREEN}/usr/local/bin/claude-code${NC}"
fi

echo ""
echo -e "运行程序: ${GREEN}./claude-code${NC}"
echo -e "查看帮助: ${GREEN}./claude-code --help${NC}"
