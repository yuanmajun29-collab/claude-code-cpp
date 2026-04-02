# Claude Code C++ - 项目说明

## 项目目标
用 C++ 重写 Anthropic Claude Code CLI 工具。

## 技术栈
- C++20, CMake 3.22+
- nlohmann/json, libcurl, OpenSSL, spdlog, CLI11
- GoogleTest (testing), Google Benchmark
- FTxUI (terminal UI, 后续阶段)

## 代码规范
- 遵循 Google C++ Style Guide
- 头文件用 `#pragma once`
- 命名空间: `claude`
- 文件命名: snake_case
- 类命名: PascalCase
- 成员函数: camelCase
- 成员变量: snake_case_

## 构建方式
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
cmake --build . -j$(nproc)
ctest --output-on-failure
```

## 开发阶段
- P0: 核心引擎 (QueryEngine, API Client, SSE Parser, Message System)
- P1: 工具系统 (10 core tools)
- P2: 终端 UI (FTxUI components)
- P3: MCP 协议
- P4: 命令系统
- P5: Agent 蜂群
- P6: 权限/沙箱
- P7: 记忆/插件/技能
- P8: IDE Bridge
- P9: 剩余工具+命令
