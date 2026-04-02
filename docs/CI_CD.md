# CI/CD - 持续集成和部署指南

本文档描述 Claude Code C++ 的 CI/CD 流程。

---

## 🔄 GitHub Actions CI

### 工作流文件
`.github/workflows/ci.yml`

### 触发条件
- Push 到 main 分支
- Pull request
- 手动触发

### CI 流程
```yaml
name: CI

on:
  push:
    branches: [main]
  pull_request:
  workflow_dispatch:

jobs:
  build:
    runs-on: ubuntu-latest
    
    steps:
      - name: Checkout
        uses: actions/checkout@v3
      
      - name: Setup CMake
        run: sudo apt-get update && sudo apt-get install -y cmake
      
      - name: Install dependencies
        run: |
          sudo apt-get install -y \
            build-essential \
            libcurl4-openssl-dev \
            libssl-dev \
            zlib1g-dev \
            pkg-config
      
      - name: Configure
        run: |
          mkdir build && cd build
          cmake .. \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_TESTS=ON \
            -DENABLE_MCP=ON \
            -DENABLE_LSP=ON \
            -DENABLE_AGENT=ON
      
      - name: Build
        run: cmake --build build -j$(nproc)
      
      - name: Test
        run: |
          cd build
          ctest --output-on-failure
      
      - name: Coverage
        if: github.event_name == 'pull_request'
        run: |
          cd build
          lcov --capture --directory . --output-file coverage.info
          lcov --remove coverage.info '/usr/*' --output-file coverage.info
          bash <(curl -s https://codecov.io/bash)
```

---

## 🚀 部署流程

### 发布检查清单
- [ ] 所有测试通过
- [ ] 无编译警告
- [ ] 代码覆盖率 > 60%
- [ ] 更新 CHANGELOG.md
- [ ] 更新版本号
- [ ] 标注 Git tag
- [ ] 创建 GitHub Release

### 发布步骤

1. **更新版本号**
   ```bash
   # 编辑 CMakeLists.txt
   project(claude-code-cpp VERSION 0.2.0 LANGUAGES CXX)
   ```

2. **更新 CHANGELOG**
   ```markdown
   ## [0.2.0] - 2026-04-03
   
   ### Added
   - Native GlobTool implementation
   - Enhanced P1 tool features
   
   ### Fixed
   - Fixed compilation issues
   - Improved error handling
   
   ### Changed
   - Improved test coverage
   ```
   
3. **创建 Git tag**
   ```bash
   git tag -a v0.2.0 -m "Release v0.2.0"
   git push origin v0.2.0
   ```

4. **创建 GitHub Release**
   ```bash
   gh release create v0.2.0 \
     --title "v0.2.0 - Enhanced Tool System" \
     --notes-file CHANGELOG.md \
     --attach build/claude-code
   ```

---

## ️ 安全检查

### 静态分析
```bash
# 使用 cppcheck
cppcheck --enable=all src/

# 使用 clang-tidy
clang-tidy src/**/*.cpp -- -I src/
```

### 依赖漏洞扫描
```bash
# 使用 dependency-check
dependency-check --scan .
```

---

## 📊 性能测试

### 基准测试
```bash
cd build
cmake .. -DBUILD_BENCHMARKS=ON
cmake --build . -j$(nproc)

# 运行基准测试
./bench_tool_performance
./bench_api_client
```

### 性能目标
- 工具执行: < 100ms
- API 调用: < 500ms
- 文件读取: < 50ms (小文件）
- 并发支持: 10+ 并发操作

---

## 📦 包管理

### Debian 包
```bash
# 创建 DEB 包
cpack -G DEB

# 安装
sudo dpkg -i claude-code-cpp_0.2.0_amd64.deb
```

### RPM 包
```bash
# 创建 RPM 包
cpack -G RPM

# 安装
sudo rpm -i claude-code-cpp-0.2.0-1.x86_64.rpm
```

### Snap 包
```bash
# 使用 Snapcraft
snapcraft

# 安装
sudo snap install claude-code-cpp_0.2.0_amd64.snap
```

---

## 🧪 测试策略

### 测试金字塔
```
        /\
       /  \
      / E2E \      (10% - 端到端测试）
     /--------\
    /  集成   \     (30% - 集成测试）
   /------------\
  /    单元      \    (60% - 单元测试）
 /----------------\
```

### 测试覆盖率目标
- 单元测试: > 80%
- 集成测试: > 70%
- 总体覆盖率: > 60%

---

**维护者:** 袁马军
**最后更新:** 2026-04-02
