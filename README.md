# 🦞 Claude Code C++

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C++-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![CMake](https://img.shields.io/badge/CMake-3.22+-green.svg)](https://cmake.org/)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS-lightgrey.svg)]()

> 用 C++ 重写的 Anthropic Claude Code CLI 工具

**Claude Code C++** 是用现代 C++20 实现的 Claude Code CLI 工具，提供高性能、低资源占用的 Claude AI 交互体验。

---

## ✨ 特性

### 🎯 核心功能
- ✅ **完整的 Anthropic API 支持** - 流式和非流式响应
- ✅ **智能工具调用循环** - 自动执行 Claude 建议的工具操作
- ✅ **文件操作工具** - 读取、写入、编辑文件
- ✅ **Shell 执行工具** - 安全的命令行操作
- ✅ **代码搜索工具** - Glob 模式匹配、Grep 内容搜索
- ✅ **上下文管理** - 智能压缩、token 追踪
- ✅ **成本追踪** - 实时显示 token 使用量和费用

### 🚀 高级特性
- 🔌 **MCP 协议支持** - Model Context Protocol
- 🔍 **LSP 客户端** - Language Server Protocol 集成
- 🤖 **Agent 群体** - 多 Agent 协作系统
- 🌉 **IDE 桥接** - 与 IDE 集成
- 🔐 **沙箱执行** - 安全的命令执行环境
- 📦 **插件系统** - 可扩展的插件架构

### 🛠️ 开发工具
- 📝 **任务管理** - TodoWrite 工具
- 🌐 **Web 工具** - 网页获取和搜索
- 🎤 **语音支持** - 语音输入/输出（可选）

---

## 📋 系统要求

### 最低要求
- **C++20 兼容编译器** (GCC 10+, Clang 12+, MSVC 2022+)
- **CMake 3.22+**
- **libcurl** (HTTP 客户端)
- **OpenSSL** (TLS/SSL)

### 推荐环境
- Linux (Ubuntu 20.04+, Debian 11+)
- macOS 12+ (Apple Silicon 或 Intel)
- 至少 4GB RAM
- 1GB 可用磁盘空间

---

## 🚀 快速开始

### 1. 安装依赖

**Ubuntu/Debian:**
```bash
sudo apt-get update
sudo apt-get install -y cmake build-essential git \
    libcurl4-openssl-dev libssl-dev pkg-config
```

**macOS:**
```bash
brew install cmake git openssl curl pkg-config
```

### 2. 克隆仓库
```bash
git clone https://github.com/yuanmajun29-collab/claude-code-cpp.git
cd claude-code-cpp
```

### 3. 构建
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

### 4. 配置 API Key
```bash
export ANTHROPIC_API_KEY="your-api-key-here"
```

或者创建配置文件 `~/.claude/config.json`:
```json
{
  "api_key": "your-api-key-here",
  "model_id": "claude-sonnet-4-20250514"
}
```

### 5. 运行
```bash
./claude-code
```

---

## 💻 使用示例

### REPL 模式
```bash
./claude-code

> 列出当前目录的文件
# Claude 会自动调用 Glob 和 FileRead 工具
```

### 单次查询
```bash
./claude-code "读取 README.md 文件"
```

### 查看帮助
```bash
./claude-code --help
```

### 流式输出
```bash
./claude-code --verbose "分析这个项目的代码结构"
```

---

## 🛠️ 构建选项

### 调试模式
```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
```

### 启用所有模块
```bash
cmake .. \
    -DBUILD_TESTS=ON \
    -DENABLE_MCP=ON \
    -DENABLE_LSP=ON \
    -DENABLE_AGENT=ON \
    -DENABLE_BRIDGE=ON \
    -DENABLE_PLUGINS=ON
```

### 禁用特定模块
```bash
cmake .. -DENABLE_VOICE=OFF -DENABLE_SANDBOX=OFF
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
ctest -R test_bash_tool
```

### 详细测试输出
```bash
ctest --verbose
```

---

## 📚 文档

- [构建指南](BUILD_GUIDE.md) - 详细的构建和配置说明
- [开发进度](DEV_PROGRESS.md) - 当前开发状态和 TODO
- [P0 核心引擎](docs/P0-core-engine.md) - 核心引擎设计文档
- [P1 工具系统](docs/P1-tools.md) - 工具系统完善计划
- [测试示例](tests/README.md) - 测试编写指南

---

## 🏗️ 项目结构

```
claude-code-cpp/
├── src/                    # 源代码
│   ├── api/               # Anthropic API 客户端
│   ├── engine/            # 查询引擎和消息处理
│   ├── tools/             # 工具实现
│   ├── app/               # 应用层（CLI、配置、生命周期）
│   ├── state/             # 状态管理
│   ├── permissions/       # 权限和沙箱
│   ├── ui/               # 用户界面
│   ├── commands/          #         命令系统
│   ├── agent/             # Agent 群体
│   ├── bridge/            # IDE 桥接
│   ├── plugins/           # 插件系统
│   └── protocol/          # 协议实现（MCP、LSP）
├── tests/                # 测试
│   ├── unit/             # 单元测试
│   ├── integration/       # 集成测试
│   └── benchmark/        # 性能测试
├── docs/                 # 设计文档
├── scripts/              # 构建和部署脚本
├── CMakeLists.txt        # CMake 配置
└── README.md             # 本文件
```

---

## 🎯 开发计划

### P0 - 核心引擎 ✅
- ✅ API 客户端（流式/非流式）
- ✅ SSE 解析器
- ✅ 查询引擎和 Tool-Call 循环
- ✅ 6 个核心工具实现
- ✅ 消息和上下文管理

### P1 - 工具系统完善 🚧
- 🚧 BashTool 增强（PTY 支持、输出格式化）
- 🚧 FileReadTool 增强（图片 base64）
- 🚧 FileWriteTool 增强（Diff 输出）
- 🚧 FileEditTool 增强（并发检测）
- 🚧 GlobTool 重写（原生实现）
- 🚧 GrepTool 增强（Context lines）

### P2 - 终端 UI 📝
- 📝 FTxUI 组件集成
- 📝 交互式 REPL
- 📝 进度显示和状态栏

### P3-P8 - 高级特性 📝
- 📝 MCP 协议完整实现
- 📝 命令系统
- 📝 Agent 群体
- 📝 权限/沙箱
- 📝 插件系统
- 📝 IDE 桥接

---

## 🤝 贡献

欢迎贡献！请遵循以下步骤：

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 开启 Pull Request

### 代码规范
- 遵循 [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
- 使用 `#pragma once` 头文件保护
- 添加单元测试覆盖新功能
- 更新相关文档

---

## 📄 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件。

---

## 🙏 致谢

- [Anthropic](https://www.anthropic.com) - Claude API 提供者
- [nlohmann/json](https://github.com/nlohmann/json) - JSON 库
- [spdlog](https://github.com/gabime/spdlog) - 日志库
- [CLI11](https://github.com/CLIUtils/CLI11) - 命令行界面

---

## 📞 联系方式

- **项目维护者:** 袁马军
- **GitHub Issues:** [提交问题](https://github.com/yuanmajun29-collab/claude-code-cpp/issues)
- **讨论区:** [GitHub Discussions](https://github.com/yuanmajun29-collab/claude-code-cpp/discussions)

---

**🦞 Made with ❤️ by the Claude Code C++ Team**
