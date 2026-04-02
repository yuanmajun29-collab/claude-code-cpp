#include "app/lifecycle.h"
#include "api/anthropic_client.h"
#include "api/auth.h"
#include "tools/tool_registry.h"
#include "tools/bash_tool.h"
#include "tools/file_read_tool.h"
#include "tools/file_write_tool.h"
#include "tools/file_edit_tool.h"
#include "tools/glob_tool.h"
#include "tools/grep_tool.h"
#include "tools/web_fetch_tool.h"
#include "tools/web_search_tool.h"
#include "tools/todo_write_tool.h"
#include "engine/cost_tracker.h"
#include "engine/query_engine.h"
#include "app/cli.h"
#include "util/platform.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <iostream>
#include <string>
#include <chrono>
#include <csignal>
#include <atomic>

namespace claude {

// Global abort flag for signal handling
static std::atomic<bool> g_interrupted{false};

static void signal_handler(int /*sig*/) {
    g_interrupted.store(true);
}

Application::Application() = default;

Application::~Application() {
    shutdown();
}

void Application::setup_logging(const AppConfig& config) {
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("claude", sink);

    if (config.verbose) {
        logger->set_level(spdlog::level::debug);
    } else {
        logger->set_level(spdlog::level::info);
    }

    spdlog::set_default_logger(logger);
    spdlog::set_pattern("%Y-%m-%d %H:%M:%S [%l] %v");
}

void Application::register_tools() {
    tools_ = std::make_unique<ToolRegistry>();

    tools_->register_tool(std::make_unique<BashTool>());
    tools_->register_tool(std::make_unique<FileReadTool>());
    tools_->register_tool(std::make_unique<FileWriteTool>());
    tools_->register_tool(std::make_unique<FileEditTool>());
    tools_->register_tool(std::make_unique<GlobTool>());
    tools_->register_tool(std::make_unique<GrepTool>());
    tools_->register_tool(std::make_unique<TodoWriteTool>());

    // Optional tools
    tools_->register_tool(std::make_unique<WebFetchTool>());
    tools_->register_tool(std::make_unique<WebSearchTool>());

    spdlog::info("Registered {} tools", tools_->size());
}

void Application::initialize(const AppConfig& config) {
    if (initialized_) return;

    config_ = config;
    setup_logging(config);

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Create state
    state_ = std::make_unique<AppState>();
    state_->set_working_directory(config.working_directory);

    // Setup session config
    auto& session = state_->current_session();
    session.model.model_id = config.model_id;
    session.model.max_tokens = config.max_tokens;
    if (config.thinking_enabled) {
        session.model.thinking = ThinkingConfig::BudgetTokens;
        session.model.thinking_budget = config.thinking_budget;
    }
    session.permission_mode = config.permission_mode;

    // Authenticate
    AuthManager auth;
    std::string api_key = config.api_key;
    if (api_key.empty()) {
        api_key = AuthManager::load_api_key_from_env();
    }
    if (api_key.empty()) {
        api_key = AuthManager::load_api_key_from_config();
    }
    if (api_key.empty()) {
        spdlog::error("No API key found. Set ANTHROPIC_API_KEY or use --api-key.");
        std::cerr << "Error: No API key found.\n"
                  << "Set ANTHROPIC_API_KEY environment variable or use --api-key flag.\n";
        return;
    }

    // Create client
    client_ = std::make_unique<AnthropicClient>(api_key);
    client_->set_base_url(config.base_url);
    client_->set_timeout(config.timeout_seconds);

    // Create cost tracker
    cost_tracker_ = std::make_unique<CostTracker>();

    // Register tools
    register_tools();

    // Create query engine
    engine_ = std::make_unique<QueryEngine>(*client_, *tools_, *state_, *cost_tracker_);
    engine_->set_max_iterations(config.max_tool_iterations);

    initialized_ = true;
    spdlog::info("Claude Code C++ initialized (model: {})", config.model_id);
}

void Application::print_banner() {
    std::cout << "\n╔═══════════════════════════════════════╗\n"
              << "║     Claude Code C++ v0.1.0              ║\n"
              << "║     AI Coding Assistant                  ║\n"
              << "╚═══════════════════════════════════════╝\n\n"
              << "Model: " << config_.model_id << "\n"
              << "Tools: " << tools_->size() << " registered\n"
              << "Type /help for commands, /quit to exit\n\n";
}

void Application::print_status() {
    auto cost = cost_tracker_->total_cost_usd();
    auto usage = cost_tracker_->total_usage();
    std::cout << "\n📊 Session Stats\n"
              << "  Tokens: " << usage.total_input() << " in, " << usage.output_tokens << " out\n"
              << "  Cost: $" << std::fixed << cost << "\n"
              << "  API calls: " << cost_tracker_->api_call_count() << "\n"
              << "  Tool calls: " << cost_tracker_->tool_call_count() << "\n";
}

int Application::run_prompt(const std::string& prompt) {
    if (!initialized_) {
        std::cerr << "Application not initialized\n";
        return 1;
    }

    spdlog::info("Executing prompt ({} chars)", prompt.size());

    auto response = engine_->execute(prompt);
    state_->update_stats(response.usage);
    state_->increment_turns();

    // Print final text content
    std::cout << response.message.text_content() << std::endl;

    print_status();
    return 0;
}

int Application::run_repl() {
    if (!initialized_) {
        std::cerr << "Application not initialized\n";
        return 1;
    }

    print_banner();
    running_ = true;

    std::string input;
    while (running_) {
        // Check for interrupt
        if (g_interrupted.exchange(false)) {
            if (engine_->is_aborted()) {
                std::cout << "\n⚠ Query already in progress, waiting...\n";
            } else {
                std::cout << "\n⚠ Use Ctrl+C again to force exit\n";
            }
        }

        // Prompt
        std::cout << "❯ " << std::flush;
        if (!std::getline(std::cin, input)) {
            // EOF
            break;
        }

        // Trim
        input.erase(0, input.find_first_not_of(" \t\n\r"));
        input.erase(input.find_last_not_of(" \t\n\r") + 1);

        if (input.empty()) continue;

        // Handle slash commands
        if (input[0] == '/') {
            if (input == "/quit" || input == "/exit" || input == "/q") {
                std::cout << "Goodbye!\n";
                break;
            } else if (input == "/help" || input == "/h") {
                std::cout << "Commands:\n"
                          << "  /help     - Show this help\n"
                          << "  /quit     - Exit\n"
                          << "  /clear    - Clear conversation\n"
                          << "  /status   - Show session stats\n"
                          << "  /model    - Show current model\n"
                          << "  /tools    - List available tools\n"
                          << "  /compact  - Compact context\n";
                continue;
            } else if (input == "/clear") {
                engine_->clear_history();
                std::cout << "✓ Conversation cleared\n";
                continue;
            } else if (input == "/status") {
                print_status();
                continue;
            } else if (input == "/model") {
                std::cout << "Model: " << config_.model_id << "\n";
                continue;
            } else if (input == "/tools") {
                for (const auto& name : tools_->tool_names()) {
                    auto* tool = tools_->find_tool(name);
                    std::cout << "  " << name << " — " << (tool ? tool->description() : "") << "\n";
                }
                continue;
            } else if (input == "/compact") {
                // Simple compaction: keep last 20 messages
                auto& msgs = engine_->messages();
                if (msgs.size() > 20) {
                    // This would need a proper compaction implementation
                    std::cout << "⚠ Compaction not yet implemented\n";
                } else {
                    std::cout << "✓ Context fits in window\n";
                }
                continue;
            } else {
                std::cout << "Unknown command: " << input << "\n";
                continue;
            }
        }

        // Execute query
        state_->increment_turns();
        auto start = std::chrono::steady_clock::now();

        auto response = engine_->execute(input);
        state_->update_stats(response.usage);

        auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();

        // Print trailing newline
        if (!response.message.text_content().empty()) {
            std::cout << std::endl;
        }

        if (response.stop_reason == "error") {
            std::cerr << "\n⚠ Error occurred\n";
        }

        spdlog::debug("Query completed in {}s (stop: {})", elapsed, response.stop_reason);
    }

    return 0;
}

void Application::shutdown() {
    if (!initialized_) return;

    running_ = false;
    if (engine_) engine_->abort();

    if (cost_tracker_) {
        auto cost = cost_tracker_->total_cost_usd();
        if (cost > 0) {
            spdlog::info("Session cost: ${:.4f}", cost);
        }
    }

    initialized_ = false;
    spdlog::info("Shutdown complete");
}

int Application::run(int argc, char** argv) {
    // Check for version/help before initialization (which requires API key)
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-V" || arg == "--version") {
            std::cout << "claude-code-cpp v0.1.0" << std::endl;
            return 0;
        }
        if (arg == "-h" || arg == "--help") {
            // Let CLI11 handle help
            break;
        }
    }

    // Parse CLI
    AppConfig config;
    try {
        config = parse_cli(argc, argv);
    } catch (const CLI::Success&) {
        return 0;
    } catch (...) {
        return 1;
    }

    // Initialize
    initialize(config);
    if (!initialized_) {
        return 1;
    }

    // Run
    int exit_code;
    if (!config.prompt.empty()) {
        exit_code = run_prompt(config.prompt);
    } else {
        exit_code = run_repl();
    }

    shutdown();
    return exit_code;
}

}  // namespace claude
