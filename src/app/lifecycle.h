#pragma once

#include "app/config.h"
#include <memory>

namespace claude {

// Forward declarations
class AnthropicClient;
class ToolRegistry;
class AppState;
class CostTracker;
class QueryEngine;

// Application lifecycle manager
class Application {
public:
    Application();
    ~Application();

    // Main entry point
    int run(int argc, char** argv);

    // Initialize all components
    void initialize(const AppConfig& config);

    // Run the REPL loop
    int run_repl();

    // Run a single prompt (non-interactive)
    int run_prompt(const std::string& prompt);

    // Graceful shutdown
    void shutdown();

    // Accessors
    AnthropicClient& client() { return *client_; }
    ToolRegistry& tools() { return *tools_; }
    AppState& state() { return *state_; }
    CostTracker& cost_tracker() { return *cost_tracker_; }
    QueryEngine& engine() { return *engine_; }

private:
    void setup_logging(const AppConfig& config);
    void register_tools();
    void print_banner();
    void print_status();

    // Components
    std::unique_ptr<AnthropicClient> client_;
    std::unique_ptr<ToolRegistry> tools_;
    std::unique_ptr<AppState> state_;
    std::unique_ptr<CostTracker> cost_tracker_;
    std::unique_ptr<QueryEngine> engine_;

    // Config
    AppConfig config_;

    // State
    bool initialized_ = false;
    bool running_ = false;
};

}  // namespace claude
