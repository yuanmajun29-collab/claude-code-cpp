#include "app/cli.h"
#include <string>
#include <iostream>

namespace claude {

void setup_cli(CLI::App& app, AppConfig& config) {
    app.set_config("--config", "", "Config file path", false);

    // API options
    app.add_option("--api-key", config.api_key, "Anthropic API key (or set ANTHROPIC_API_KEY)")
        ->envname("ANTHROPIC_API_KEY")
        ->configurable(false);

    app.add_option("--model,-m", config.model_id, "Model ID to use")
        ->envname("CLAUDE_CODE_MODEL");

    app.add_option("--base-url", config.base_url, "API base URL")
        ->envname("ANTHROPIC_BASE_URL");

    app.add_option("--max-tokens", config.max_tokens, "Maximum tokens per response")
        ->envname("CLAUDE_CODE_MAX_TOKENS")
        ->check(CLI::Range(1, 1000000));

    app.add_option("--timeout", config.timeout_seconds, "Request timeout in seconds")
        ->check(CLI::Range(1, 3600));

    // Behavior options
    app.add_option("--permission-mode,-p", config.permission_mode,
                   "Permission mode (default, bypass, yolo)")
        ->envname("CLAUDE_CODE_PERMISSION_MODE");

    app.add_flag("--auto-compact", config.auto_compact, "Enable automatic context compaction");
    app.add_option("--max-iterations", config.max_tool_iterations,
                   "Maximum tool-call loop iterations")
        ->check(CLI::Range(1, 10000));

    // Thinking options
    app.add_flag("--thinking", config.thinking_enabled, "Enable extended thinking");
    app.add_option("--thinking-budget", config.thinking_budget,
                   "Budget tokens for thinking")
        ->check(CLI::Range(1, 200000));

    // Display options
    app.add_option("--theme,-t", config.theme, "UI theme (dark, light)");
    app.add_flag("-v,--verbose", config.verbose, "Verbose output");
    app.add_flag("--no-color,!--color", config.no_color, "Disable colored output");

    // Non-interactive mode
    app.add_option("prompt", config.prompt, "Prompt to send (non-interactive mode)");
}

AppConfig parse_cli(int argc, char** argv) {
    AppConfig config = AppConfig::load();
    CLI::App app{"Claude Code C++ — AI coding assistant"};

    setup_cli(app, config);

    // Version flag - handle before parse
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-V" || arg == "--version") {
            std::cout << "claude-code-cpp v0.1.0" << std::endl;
            throw CLI::Success();
        }
        if (arg == "-h" || arg == "--help") {
            // Let CLI11 handle help display
            try {
                app.parse(argc, argv);
            } catch (const CLI::CallForHelp&) {
                std::cout << app.help() << std::endl;
                throw CLI::Success();
            }
        }
    }

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        if (e.get_exit_code() != 0) {
            std::cerr << app.help() << std::endl;
            throw;
        }
    }

    // Apply --no-color flag
    if (config.no_color) {
        config.color = false;
    }

    return config;
}

}  // namespace claude
