# CONTRIBUTING - 贡献指南

感谢你对 Claude Code C++ 的兴趣！本文档帮助您开始贡献。

---

## 🤝 如何贡献

### 报告 Bug
1. 在 [Issues](https://github.com/yuanmajun29-collab/claude-code-cpp/issues) 搜索现有问题
2. 如果没有，创建新 Issue，使用 Bug 模板
3. 提供详细的信息：
   - 操作系统和版本
   - CMake 和编译器版本
   - 复现步骤
   - 预期行为
   - 实际行为
   - 错误日志

### 功能请求
1. 在 [Issues](https://github.com/yuanmajun29-collab/claude-code-cpp/issues) 搜索现有请求
2. 如果没有，创建新 Issue，使用 Feature Request 模板
3. 描述功能的使用场景和益处

### 提交代码
1. Fork 仓库
2. 创建特性分支：`git checkout -b feature/AmazingFeature`
3. 遵循代码规范
4. 编写测试
5. 提交更改
6. 推送到分支
7. 创建 Pull Request

---

## 📋 开发流程

### 设置开发环境
```bash
# 克隆仓库
git clone https://github.com/your-username/claude-code-cpp.git
cd claude-code-cpp

# 安装依赖
./scripts/setup-env.sh

# 构建项目
./build.sh
```

### 分支命名
- `feature/` 功能开发
- `bugfix/` Bug 修复
- `refactor/` 代码重构
- `test/` 测试相关
- `docs/` 文档更新

### 提交信息格式
```
<type>(<scope>): <subject>

<body>

<footer>
```

**类型 (type):**
- `feat`: 新功能
- `fix`: Bug 修复
- `docs`: 文档变更
- `style`: 代码格式（不影响功能）
- `refactor`: 重构
- `test`: 测试相关
- `chore`: 构建/工具相关

**示例:**
```
feat(grep): add file type filter support

Added --type parameter to GrepTool for filtering
by file type (cpp, rust, py, etc.).

Closes #123
```

---

## 🎨 代码规范

### C++ 风格
- 遵循 [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
- 使用 C++20 特性
- 每行最多 100 字符
- 使用 `#pragma once` 头文件保护
- 使用 `nullptr` 代替 `NULL`

### 命名约定
- **类名:** `PascalCase` (如 `ToolRegistry`)
- **函数名:** `camelCase` (如 `readFile`)
- **变量名:** `snake_case_` (如 `api_key_`)
- **常量:** `kPascalCase` (如 `kMaxFileSize`)
- **文件名:** `snake_case` (如 `file_read_tool.cpp`)

### 头文件
```cpp
#pragma once

#include <standard_headers>

namespace claude {

class ClassName {
public:
    // Public interface
private:
    // Private implementation
};

}  // namespace claude
```

### 错误处理
```cpp
// 使用 optional 返回
std::optional<T> function();

// 使用异常处理
try {
    // ...
} catch (const std::exception& e) {
    // Log error
    return ToolOutput::err(e.what());
}
```

---

## 🧪 测试规范

### 单元测试
- 使用 Google Test
- 测试文件命名: `test_<module>.cpp`
- 测试类命名: `<Module>Test`
- 测试用例命名: `TestName_Descriptive`

```cpp
#include <gtest/gtest.h>
#include "module.h"

namespace claude {
namespace test {

class ModuleTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ModuleTest, TestName) {
    // Arrange
    // Act
    // Assert
}

}  // namespace test
}  // namespace claude
```

### 测试覆盖率
- 目标覆盖率: > 60%
- 关键模块: > 80%
- 新功能: 必须有测试

---

## 📝 文档规范

### 代码注释
```cpp
// 单行注释：简短描述

/*
 * 多行注释：详细解释
 * 可以跨越多行
 */

/// 函数/类文档：Doxygen 风格
/// @param param1 参数说明
/// @return 返回值说明
std::string function(int param1);
```

### Markdown 文档
- 使用清晰的标题结构
- 提供代码示例
- 使用代码块语法高亮
- 包含"如何使用"部分

---

## 🔍 Pull Request 流程

### PR 检查清单
- [ ] 通过所有 CI 检查
- [ ] 代码格式符合规范
- [ ] 有相应的测试
- [ ] 更新了文档
- [ ] Commit 信息清晰
- [ ] 无合并冲突

### PR 模板
```markdown
## 描述
简要描述这个 PR 的目的。

## 变更类型
- [ ] Bug 修复
- [ ] 新功能
- [ ] 重构
- [ ] 文档更新
- [ ] 性能优化

## 相关 Issue
Closes #<issue_number>

## 测试
- [ ] 添加了测试
- [ ] 所有测试通过

## 截图/示例
（如果适用）
```

### 代码审查
- 所有 PR 需要至少 1 个审查
- 解决所有审查意见
- 主开发者批准后合并

---

## 📜 Issue 模板

### Bug 报告
```markdown
**描述:**
清晰的 Bug 描述

**复现步骤:**
1. 步骤 1
2. 步骤 2
3. ...

**预期行为:**
应该发生什么

**实际行为:**
实际发生了什么

**环境:**
- OS: [e.g., Ubuntu 22.04]
)
- CMake 版本: [e.g., 3.28.0]
- 编译器: [e.g., GCC 13.3]
- 构建类型: [e.g., Debug/Release]

**日志:**
```

### 功能请求
```markdown
**问题描述:**
当前缺少什么功能

**建议解决方案:**
你希望如何实现

**使用场景:**
为什么需要这个功能

**替代方案:**
你尝试过哪些替代方案
```

---

## 💡 最佳实践

### 性能
- 避免不必要的拷贝
- 使用引用传递大对象
- 使用移动语义
- 缓存频繁使用的值

### 安全
- 验证输入
- 使用安全的字符串操作（防止缓冲区溢出）
- 检查查径遍历攻击
- 避免危险操作

### 可维护性
- 保持函数简短（< 50 行）
- 单一职责原则
- 避免深度嵌套（< 4 层）
- 使用有意义的命名

---

## 📞 获取帮助

- **Issue:** [提交问题](https://github.com/yuanmajun29-collab/claude-code-cpp/issues)
- **Discussions:** [参与讨论](https://github.com/yuanmajun29-collab/claude-code-cpp/discussions)
- **Email:** yuanmajun29@gmail.com

---

**感谢你的贡献！** 🦞
