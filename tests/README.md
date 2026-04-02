# Claude Code C++ - 测试示例

这是一个简单的单元测试示例，展示如何使用 Google Test 测试项目组件。

## 测试结构

```cpp
#include <gtest/gtest.h>
#include "tools/bash_tool.h"
#include "util/string_utils.h"

namespace claude {
namespace test {

class BashToolTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 初始化测试
        tool = std::make_unique<BashTool>();
    }

    void TearDown() override {
        // 清理
    }

    std::unique_ptr<BashTool> tool;
    ToolContext context;
};

TEST_F(BashToolTest, InputSchemaValidation) {
    auto schema = tool->input_schema();
    EXPECT_FALSE(schema.type.empty());
    EXPECT_EQ(schema.name, "Bash");
    EXPECT_FALSE(schema.description.empty());
}

TEST_F(BashToolTest, SimpleCommandExecution) {
    std::string input = R"({
        "command": "echo 'Hello, World!'"
    })";

    auto output = tool->execute(input, context);
    EXPECT_FALSE(output.error);
    EXPECT_FALSE(output.content.empty());
}

TEST_F(BashToolTest, DangerousCommandDetection) {
    std::string input = R"({
        "command": "rm -rf /"
    })";

    // 需要权限检查
    auto output = tool->execute(input, context);
    // 在实际测试中应该检查权限机制
}

}  // namespace test
}  // namespace claude
```

## 测试文件位置

将测试文件放在以下目录：
- 单元测试: `tests/unit/`
- 集成测试: `tests/integration/`
- 性能测试: `tests/benchmark/`

## 运行测试

```bash
# 编译测试
cd build
cmake --build . -j$(nproc)

# 运行所有测试
ctest --output-on-failure

# 运行特定测试
ctest -R test_bash_tool

# 详细输出
ctest --verbose
```

## 测试最佳实践

1. **命名规范**: 使用 `TestSuite.TestName` 格式
2. **独立性**: 每个测试应该独立运行
3. **清晰性**: 测试名称应该说明测试内容
4. **快速性**: 单元测试应该快速完成
5. **可重复**: 测试结果应该可重复

## Mock 和桩

对于需要外部依赖的测试，使用 Google Mock：

```cpp
#include <gmock/gmock.h>

class MockAnthropicClient : public AnthropicClient {
public:
    MOCK_METHOD(QueryResponse, create_message, (const QueryOptions&), (override));
    MOCK_METHOD(void, create_message_stream,
               (const QueryOptions&, StreamCallback), (override));
};

TEST(QueryEngineTest, HandlesToolCallLoop) {
    MockAnthropicClient mock_client;
    // 设置 mock 行为
    EXPECT_CALL(mock_client, create_message(testing::_))
        .WillOnce(Return(expected_response));

    // 测试代码
}
```

## 覆盖率报告

生成测试覆盖率：

```bash
# 启用覆盖率编译
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
cmake --build . -j$(nproc)
ctest

# 生成报告
lcov --capture --directory . --output-file coverage.info
lcov --remove coverage.info '/usr/*' --output-file coverage.info
genhtml coverage.info --output-directory coverage_html

# 查看报告
xdg-open coverage_html/index.html
```
