#include <gtest/gtest.h>
#include "engine/message.h"
#include "tools/tool_registry.h"
#include "tools/bash_tool.h"
#include "tools/file_read_tool.h"
#include "tools/file_write_tool.h"
#include "nlohmann/json.hpp"
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace claude {
namespace test {

class ToolIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        registry = std::make_unique<ToolRegistry>();
        
        // 注册核心工具
        registry->register_tool(std::make_unique<BashTool>());
        registry->register_tool(std::make_unique<FileReadTool>());
        registry->register_tool(std::make_unique<FileWriteTool>());
        
        // 设置测试目录
        test_dir = fs::temp_directory_path() / "claude_test_" + 
                   std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        fs::create_directories(test_dir);
        
        context.working_directory = test_dir.string();
    }

    void TearDown() override {
        // 清理测试目录
        if (fs::exists(test_dir)) {
            fs::remove_all(test_dir);
        }
    }

    std::unique_ptr<ToolRegistry> registry;
    ToolContext context;
    fs::path test_dir;
};

TEST_F(ToolIntegrationTest, ToolRegistryInitialization) {
    EXPECT_GT(registry->tool_count(), 0);
    EXPECT_TRUE(registry->has_tool("Bash"));
    EXPECT_TRUE(registry->has_tool("FileRead"));
    EXPECT_TRUE(registry->has_tool("FileWrite"));
}

TEST_F(ToolIntegrationTest, BashToolEcho) {
    auto tool = registry->get_tool("Bash");
    ASSERT_NE(tool, nullptr);
    
    std::string input = R"({
        "command": "echo 'Hello from test!'"
    })";
    
    auto output = tool->execute(input, context);
    EXPECT_FALSE(output.error);
    EXPECT_TRUE(output.content.find("Hello from test!") != std::string::npos);
}

TEST_F(ToolIntegrationTest, FileWriteAndRead) {
    auto write_tool = registry->get_tool("FileWrite");
    auto read_tool = registry->get_tool("FileRead");
    
    ASSERT_NE(write_tool, nullptr);
    ASSERT_NE(read_tool, nullptr);
    
    // 写入文件
    fs::path test_file = test_dir / "test.txt";
    std::string write_input = json({
        {"path", test_file.string()},
        {"content", "Test content for integration test"}
    }).dump();
    
    auto write_output = write_tool->execute(write_input, context);
    EXPECT_FALSE(write_output.error);
    
    // 读取文件
    std::string read_input = json({
        {"path", test_file.string()}
    }).dump();
    
    auto read_output = read_tool->execute(read_input, context);
    EXPECT_FALSE(read_output.error);
    EXPECT_TRUE(read_output.content.find("Test content for integration test") != std::string::npos);
}

TEST_F(ToolIntegrationTest, BashToolChangeDirectory) {
    auto tool = registry->get_tool("Bash");
    ASSERT_NE(tool, nullptr);
    
    // 创建子目录
    fs::path subdir = test_dir / "subdir";
    fs::create_directories(subdir);
    
    // 切换目录
    std::string cd_input = json({
        {"command", "cd " + subdir.string()}
    }).dump();
    
    auto cd_output = tool->execute(cd_input, context);
    EXPECT_FALSE(cd_output.error);
}

TEST_F(ToolIntegrationTest, MessageBuilder) {
    Message msg = Message::user("Hello, Claude!");
    EXPECT_EQ(msg.role, "user");
    EXPECT_EQ(msg.content[0].type, "text");
    EXPECT_EQ(msg.content[0].text, "Hello, Claude!");
    
    json j = msg;
    EXPECT_TRUE(j.is_object());
    EXPECT_EQ(j["role"], "user");
}

TEST_F(ToolIntegrationTest, ToolInputSchema) {
    auto bash_tool = registry->get_tool("Bash");
    auto schema = bash_tool->input_schema();
    
    EXPECT_FALSE(schema.type.empty());
    EXPECT_FALSE(schema.name.empty());
    EXPECT_FALSE(schema.description.empty());
    EXPECT_FALSE(schema.required.empty());
    
    // Bash tool 应该有 command 参数
    EXPECT_TRUE(schema.properties.find("command") != schema.properties.end());
}

TEST_F(ToolIntegrationTest, ToolSystemPrompt) {
    auto bash_tool = registry->get_tool("Bash");
    auto prompt = bash_tool->system_prompt();
    
    EXPECT_FALSE(prompt.empty());
    EXPECT_TRUE(prompt.find("Bash") != std::string::npos);
    EXPECT_TRUE(prompt.find("command") != std::string::npos);
}

}  // namespace test
}  // namespace claude

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
