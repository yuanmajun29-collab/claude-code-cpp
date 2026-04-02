#pragma once

#include "engine/message.h"
#include <string>
#include <unordered_map>
#include <mutex>

namespace claude {

// Cost tracker for token usage and费用
class CostTracker {
public:
    CostTracker() = default;

    // Record token usage from a single API call
    void record_usage(const TokenUsage& usage);

    // Record usage by model
    void record_model_usage(const std::string& model, const TokenUsage& usage);

    // Get total accumulated usage
    TokenUsage total_usage() const;

    // Get total cost in USD
    double total_cost_usd() const;

    // Get per-model usage
    std::unordered_map<std::string, TokenUsage> usage_by_model() const;

    // Reset all tracking
    void reset();

    // Format cost for display
    std::string format_cost() const;

    // Format usage summary
    std::string format_usage() const;

    // Total API duration tracking
    void record_api_duration(double duration_ms);
    double total_api_duration_ms() const { return total_api_duration_ms_; }

    // Total tool execution duration
    void record_tool_duration(double duration_ms);
    double total_tool_duration_ms() const { return total_tool_duration_ms_; }

    // Number of API calls
    int api_call_count() const { return api_call_count_; }

    // Number of tool calls
    int tool_call_count() const { return tool_call_count_; }
    void increment_tool_calls() { tool_call_count_++; }

private:
    mutable std::mutex mutex_;

    // Accumulated usage
    int64_t total_input_tokens_ = 0;
    int64_t total_output_tokens_ = 0;
    int64_t total_cache_creation_tokens_ = 0;
    int64_t total_cache_read_tokens_ = 0;

    // Per-model usage
    std::unordered_map<std::string, TokenUsage> model_usage_;

    // Duration tracking
    double total_api_duration_ms_ = 0.0;
    double total_tool_duration_ms_ = 0.0;

    // Counters
    int api_call_count_ = 0;
    int tool_call_count_ = 0;
};

}  // namespace claude
