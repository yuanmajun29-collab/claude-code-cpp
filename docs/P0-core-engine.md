# P0 — 核心引擎开发任务书

## 负责团队
核心开发团队 (cqfz-core-develop)

## 阶段目标
构建 Claude Code C++ 的核心引擎，实现与 Anthropic API 的完整通信链路：
**API 调用 → SSE 流式解析 → Tool-Call 循环 → 消息管理 → 会话管理**

## 已完成
- [x] 项目目录结构 `/mnt/projects/claude-code-cpp/`
- [x] CMakeLists.txt（含依赖、编译选项、模块开关）
- [x] `engine/message.h` — 核心类型定义（Message, TokenUsage, StreamEvent, ModelConfig 等）
- [x] `tools/tool_base.h` — 工具抽象基类（ToolBase, ToolContext, ToolOutput）
- [x] `tools/tool_registry.h/.cpp` — 工具注册中心
- [x] `engine/message.cpp` — 消息构建器
- [x] `.claude/CLAUDE.md` — 项目配置

## 待开发文件清单

### 1. API 客户端 (`src/api/`)

#### `src/api/anthropic_client.h`
Anthropic API 客户端类，核心接口：
```cpp
class AnthropicClient {
public:
    explicit AnthropicClient(const std::string& api_key);
    
    // 非流式请求
    QueryResponse create_message(const QueryOptions& options);
    
    // 流式请求（回调模式）
    using StreamCallback = std::function<void(const StreamEvent&)>;
    void create_message_stream(const QueryOptions& options, StreamCallback callback);
    
    // 配置
    void set_base_url(const std::string& url);
    void set_timeout(int seconds);
    void set_proxy(const std::string& proxy);
    
    // 认证
    static std::string load_api_key_from_env();
    static std::string load_api_key_from_config();
    
private:
    std::string api_key_;
    std::string base_url_ = "https://api.anthropic.com";
    int timeout_seconds_ = 120;
    std::string proxy_;
};
```

#### `src/api/sse_parser.h`
SSE (Server-Sent Events) 流解析器：
```cpp
class SSEParser {
public:
    // 解析一行 SSE 数据
    // data: {"type":"content_block_delta",...}
    // event: message_delta
    // id: evt_xxx
    struct SSELine { std::string event, data, id; };
    
    std::optional<SSELine> parse_line(const std::string& line);
    
    // 将 data JSON 解析为 StreamEvent
    StreamEvent parse_event(const std::string& event_type, const std::string& json_data);
    
    // 累积式工具调用 JSON 拼接
    std::string accumulate_tool_input(const std::string& delta);
};
```

#### `src/api/auth.h`
认证管理（OAuth 2.0 + API Key）：
```cpp
class AuthManager {
public:
    enum class AuthType { ApiKey, OAuth };
    
    AuthResult authenticate(AuthType type);
    void refresh_token();
    bool is_authenticated() const;
    std::string get_valid_token();
};
```

### 2. 查询引擎 (`src/engine/`)

#### `src/engine/query_engine.h`
核心查询引擎，参考原版 `QueryEngine.ts`（46KB）：
```cpp
class QueryEngine {
public:
    QueryEngine(AnthropicClient& client, ToolRegistry& tools, AppState& state);
    
    // 主查询入口 — 实现 tool-call 循环
    QueryResponse execute(const QueryOptions& options);
    
    // 流式执行
    void execute_stream(const QueryOptions& options, 
                        std::function<void(const StreamEvent&)> on_event,
                        std::function<void(const QueryResponse&)> on_complete);
    
    // 上下文管理
    void add_user_message(const std::string& text);
    void add_system_message(const std::string& text);
    
    // 工具执行
    ToolOutput execute_tool(const ToolCall& call);
    
    // 压缩/截断
    void compact_context();
    
    // 中止当前查询
    void abort();
    
private:
    // tool-call 循环核心
    QueryResponse run_tool_call_loop(const QueryOptions& options);
    
    // 权限检查
    PermissionResult check_tool_permission(const ToolCall& call);
};
```

#### `src/engine/query_pipeline.h`
查询管线，参考原版 `query.ts`（69KB）：
```cpp
class QueryPipeline {
public:
    // 构建 API 请求体
    nlohmann::json build_request_body(const QueryOptions& options);
    
    // 消息格式化
    std::vector<nlohmann::json> format_messages(const std::vector<Message>& messages);
    
    // 系统提示构建
    std::string build_system_prompt(const std::vector<SystemPrompt>& prompts);
    
    // 上下文窗口管理
    bool needs_compaction(const std::vector<Message>& messages, int64_t max_tokens);
    
    // 消息裁剪（保留最近 N 条 + system prompt）
    std::vector<Message> trim_messages(std::vector<Message> messages, int max_messages);
};
```

#### `src/engine/context_builder.h`
上下文构建器，参考原版 `context.ts`：
```cpp
class ContextBuilder {
public:
    // 从工作目录收集上下文
    SystemPrompt build_system_context(const SessionConfig& config);
    SystemPrompt build_user_context(const SessionConfig& config);
    
    // 读取 CLAUDE.md 配置
    std::string read_claude_md(const std::string& directory);
    
    // Git 状态
    std::string get_git_status(const std::string& cwd);
    
    // 环境信息
    std::string get_environment_info();
};
```

#### `src/engine/cost_tracker.h`
Token 费用追踪，参考原版 `cost-tracker.ts`：
```cpp
class CostTracker {
public:
    void record_usage(const TokenUsage& usage);
    TokenUsage total_usage() const;
    double total_cost_usd() const;
    void reset();
    std::string format_cost() const;
    
    // 按模型分别统计
    void record_model_usage(const std::string& model, const TokenUsage& usage);
    std::unordered_map<std::string, TokenUsage> usage_by_model() const;
};
```

#### `src/engine/stream_parser.h`
将 Anthropic SSE 事件解析为内部 StreamEvent，已部分在 `api/sse_parser.h` 定义。

### 3. 应用层 (`src/app/`)

#### `src/app/config.h`
配置管理：
```cpp
struct AppConfig {
    std::string api_key;
    std::string model_id = "claude-sonnet-4-20250514";
    std::string base_url = "https://api.anthropic.com";
    std::string permission_mode = "default";
    std::string theme = "dark";
    int max_context_messages = 100;
    bool auto_compact = true;
    // ...
    
    static AppConfig load_from_file(const std::string& path);
    void save_to_file(const std::string& path) const;
    static AppConfig load_from_env();
};
```

#### `src/app/cli.h`
CLI 入口（CLI11）：
```cpp
void setup_cli(CLI::App& app, AppConfig& config);
// 支持: claude-code [options] [prompt]
// --model, --api-key, --max-tokens, --permission-mode, etc.
```

#### `src/app/lifecycle.h`
应用生命周期管理：
```cpp
class Application {
public:
    int run(int argc, char** argv);
private:
    void initialize();
    void shutdown();
    // ...
};
```

### 4. 状态管理 (`src/state/`)

#### `src/state/app_state.h`
全局应用状态：
```cpp
class AppState {
public:
    // Session 管理
    SessionConfig& current_session();
    SessionStats& stats();
    
    // 消息历史
    std::vector<Message>& messages();
    void push_message(const Message& msg);
    
    // 配置变更通知
    using ConfigCallback = std::function<void()>;
    void on_config_change(ConfigCallback cb);
    
private:
    std::vector<Message> messages_;
    SessionConfig session_config_;
    SessionStats session_stats_;
    CostTracker cost_tracker_;
    std::vector<ConfigCallback> config_callbacks_;
};
```

### 5. 工具实现 (`src/tools/`) — P0 核心工具

#### `src/tools/bash_tool.h`
Shell 命令执行工具：
- posix_spawn 执行命令
- 超时控制
- 输出截断
- 安全检查（禁止 rm -rf / 等）

#### `src/tools/file_read_tool.h`
文件读取工具：
- 纯文本文件
- 二进制文件（图片 base64）
- 支持 offset/limit
- 行号标注

#### `src/tools/file_write_tool.h`
文件写入工具：
- 创建新文件
- 覆盖整个文件

#### `src/tools/file_edit_tool.h`
文件编辑工具（参考原版 FileEditTool，最复杂的工具之一）：
- old_string → new_string 替换
- 多处替换
- 行号定位
- diff 输出

#### `src/tools/glob_tool.h`
文件模式匹配：
- 调用系统 glob 或自己实现
- 排除 .git/node_modules 等

#### `src/tools/grep_tool.h`
内容搜索工具：
- 调用 ripgrep (rg) 子进程
- 正则支持
- 文件类型过滤

## 原版参考文件路径
源码位于 `/mnt/opensource/claude-code-main/src/`：

| C++ 文件 | 对应原版 | 行数 |
|----------|---------|------|
| api/anthropic_client.h | services/api/claude.ts | ~大 |
| api/sse_parser.h | query.ts (SSE解析部分) | ~69K |
| engine/query_engine.h | QueryEngine.ts | ~46K |
| engine/query_pipeline.h | query.ts | ~69K |
| engine/context_builder.h | context.ts | ~6K |
| engine/cost_tracker.h | cost-tracker.ts | ~11K |
| tools/bash_tool.h | tools/BashTool/ | 16 文件 |
| tools/file_edit_tool.h | tools/FileEditTool/ | 6 文件 |
| tools/file_read_tool.h | tools/FileReadTool (单文件) | - |
| tools/grep_tool.h | tools/GrepTool (单文件) | - |
| tools/glob_tool.h | tools/GlobTool (单文件) | - |

## 交付标准
1. ✅ `cmake --build .` 零错误编译通过
2. ✅ `ctest` 全部测试通过
3. ✅ 能向 Anthropic API 发送消息并接收流式响应
4. ✅ Tool-Call 循环完整运行（model 调用工具 → 执行 → 返回 → 继续）
5. ✅ 6 个核心工具可用（Bash, FileRead, FileWrite, FileEdit, Glob, Grep）
6. ✅ CLI 启动并进入 REPL 交互模式
7. ✅ 配置文件读写正常
8. ✅ Token 用量追踪和费用计算

## 预估工期
60 人天
