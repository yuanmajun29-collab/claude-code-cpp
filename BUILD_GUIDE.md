# Claude Code C++ - 构建指南

本文档详细说明如何构建、测试和运行 Claude Code C++ 项目。

---

## 📋 前置要求

### 必需组件
- **CMake 3.22+** - 构建系统
- **C++20 兼容编译器** - GCC 10+, Clang 12+, MSVC 2022+
- **Git** - 版本控制
- **Make** - 构建工具（或 Ninja）

### 系统库依赖
- **libcurl** - HTTP 客户端
- **OpenSSL** - TLS/SSL 支持
- **zlib** - 压缩支持

### 可选组件
- **Google Test** - 单元测试框架（BUILD_TESTS=ON）
- **Google Benchmark** - 性能测试（BUILD_BENCHMARKS=ON）

---

## 🚀 快速开始

### 1. 环境设置（首次使用）

运行自动设置脚本：
```bash
./scripts/setup-env.sh
```

手动安装（Ubuntu/Debian）：
```bash
sudo apt-get update
sudo apt-get install -y \
    cmake \
    build-essential \
    git \
    libcurl4-openssl-dev \
    libssl-dev \
    zlib1g-dev \
    pkg-config
```

手动安装（macOS）：
```bash
brew install cmake git openssl curl pkg-config
```

### 2. 克隆仓库
```bash
git clone https://github.com/yuanmajun29-collab/claude-code-cpp.git
cd claude-code-cpp
```

### 3. 配置构建
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
```

### 4. 编译
```bash
cmake --build . -j$(nproc)
```

### 5. 运行
```bash
./claude-code
```

---

## ⚙️ 构建配置选项

### 构建类型
```bash
# Debug 模式（包含调试符号，无优化）
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Release 模式（优化，生产使用）
cmake .. -DCMAKE_BUILD_TYPE=Release

# RelWithDebInfo 模式（优化 + 调试符号）
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

### 模块开关

| 选项 | 默认值 | 说明 |
|------|--------|------|
| BUILD_TESTS | ON | 构建测试套件 |
| BUILD_BENCHMARKS | OFF | 构建性能测试 |
| ENABLE_MCP | ON | 启用 MCP 协议支持 |
| ENABLE_LSP | ON | 启用 LSP 客户端支持 |
| ENABLE_BRIDGE | ON | 启用 IDE 桥接支持 |
| ENABLE_AGENT | ON | 启用 Agent 群体支持 |
| ENABLE_PLUGINS | ON | 启用插件系统 |
| ENABLE_VOICE | OFF | 启用语音输入/输出 |
| ENABLE_SANDBOX | ON | 启用沙箱执行 |

示例：
```bash
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=ON \
    -DENABLE_VOICE=OFF \
    -DENABLE_AGENT=ON
```

### 自定义安装路径
```bash
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build . --target install
```

---

## 🧪 测试

### 运行所有测试
```bash
cd build
ctest --output-on-failure
```

### 运行特定测试
```bash
# 列出所有测试
ctest -N

# 运行指定测试
ctest -R test_query_engine

# 详细输出
ctest --verbose

# 并行运行
ctest -j$(nproc)
```

### 测试覆盖率
需要安装 `lcov`：
```bash
sudo apt-get install lcov

# 重新构建并测试（启用覆盖率）
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
cmake --build . -j$(nproc)
ctest

# 生成覆盖率报告
lcov --capture --directory . --output-file coverage.info
lcov --remove coverage.info '/usr/lib/*' --output-file coverage.info
lcov --list coverage.info
genhtml coverage.info --output-directory coverage_html

# 查看报告
xdg-open coverage_html/index.html
```

---

## 🔍 调试

### 使用 GDB
```bash
cd build
gdb ./claude-code
# 在 gdb 中:
(gdb) run
(gdb) bt  # 查看调用栈
(gdb) p variable  # 打印变量
```

### 使用 LLDB（macOS）
```bash
cd build
lldb ./claude-code
# 在 lldb 中:
(lldb) run
(lldb) bt
(lldb) expr variable
```

### 日志级别
通过环境变量或命令行参数控制：
```bash
# 设置日志级别
export SPDLOG_LEVEL=debug

# 启用详细输出
./claude-code --verbose
```

---

## 📦 依赖管理

### FetchContent 依赖
项目使用 CMake FetchContent 自动下载以下依赖：

| 依赖 | 版本 | URL |
|------|------|-----|
| nlohmann/json | v3.11.3 | https://github.com/nlohmann/json |
| spdlog | v1.14.1 | https://github.com/gabime/spdlog |
| CLI11 | v2.4.2 | https://github.com/CLIUtils/CLI11 |
| GoogleTest | v1.14.0 | https://github.com/google/googletest |
| Google Benchmark | v1.8.3 | https://github.com/google/benchmark |

这些依赖会自动下载到 `build/_deps/` 目录。

### 离线构建
如果需要离线构建：
```bash
# 预下载所有依赖
cmake .. -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/path/to/googletest
cmake .. -DFETCHCONTENT_SOURCE_DIR_JSON=/path/to/json
# ... 其他依赖
```

---

## 🛠️ 常见问题

### 问题 1: CMake 版本过低
**错误:** `CMake 3.22 or higher is required`

**解决:**
```bash
# Ubuntu/Debian
wget https://github.com/Kitware/CMake/releases/download/v3.29.0/cmake-3.29.0-linux-x86_64.sh
sudo sh cmake-3.29.0-linux-x86_64.sh --prefix=/usr/local --skip-license

# macOS
brew upgrade cmake
```

### 问题 2: 找不到 libcurl
**错误:** `Could not find CURL`

**解决:**
```bash
# Ubuntu/Debian
sudo apt-get install libcurl4-openssl-dev

# macOS
brew install curl
```

### 问题 3: OpenSSL 找不到
**错误:** `Could not find OpenSSL`

**解决:**
```bash
# Ubuntu/Debian
sudo apt-get install libssl-dev

# macOS
brew install openssl
export OPENSSL_ROOT_DIR=$(brew --prefix openssl)
```

### 问题 4: 编译错误 - C++20 特性
**错误:** `error: 'constexpr' function does not produce constant expression`

**解决:**
确保使用支持 C++20 的编译器：
```bash
# 检查 GCC 版本
g++ --version  # 需要 10.0+

# 检查 Clang 版本
clang++ --version  # 需要 12.0+
```

### 问题 5: 链接错误 - undefined reference
**错误:** `undefined reference to 'curl_easy_setopt'`

**解决:**
```bash
# 确保已安装开发库（带 -dev）
sudo apt-get install libcurl4-openssl-dev

# 清理并重新构建
cd build
rm -rf *
cmake ..
cmake --build . -j$(nproc)
```

---

## 🚀 性能优化

### 编译器优化选项
```bash
# 启用链接时优化（LTO）
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_LTO=ON

# 使用 PGO（需要先运行典型工作负载）
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_PGO=ON
cmake --build . -j$(nproc)
./generate_pgo_data  # 运行典型工作负载
cmake --build . -j$(nproc)  # 使用 PGO 数据重新编译
```

### 并行编译
```bash
# 使用所有可用核心
cmake --build . -j$(nproc)

# 使用 Ninja（更快）
cmake .. -GNinja
ninja -j$(nproc)
```

---

## 📝 构建脚本

### 完整构建脚本
```bash
#!/bin/bash
# build.sh - 一键构建脚本

set -e

echo "🦞 构建 Claude Code C++"

# 创建构建目录
mkdir -p build
cd build

# 配置
echo "🔧 配置 CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=ON \
    -DENABLE_AGENT=ON

# 编译
echo "🏗️  编译..."
cmake --build . -j$(nproc)

# 测试
echo "🧪 运行测试..."
ctest --output-on-failure



echo "✅ 构建完成！"
echo "可执行文件: $(pwd)/claude-code"
```

使用：
```bash
chmod +x build.sh
./build.sh
```

---

## 📚 更多资源

- [CMake 官方文档](https://cmake.org/documentation/)
- [项目文档](docs/)
- [开发进度](DEV_PROGRESS.md)
- [GitHub Issues](https://github.com/yuanmajun29-collab/claude-code-cpp/issues)

---

**维护者:** 袁马军
**最后更新:** 2026-04-02
