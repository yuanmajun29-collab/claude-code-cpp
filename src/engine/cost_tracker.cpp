#include "engine/cost_tracker.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace claude {

void CostTracker::record_usage(const TokenUsage& usage) {
    std::lock_guard<std::mutex> lock(mutex_);
    total_input_tokens_ += usage.input_tokens;
    total_output_tokens_ += usage.output_tokens;
    total_cache_creation_tokens_ += usage.cache_creation_input_tokens;
    total_cache_read_tokens_ += usage.cache_read_input_tokens;
    api_call_count_++;
}

void CostTracker::record_model_usage(const std::string& model, const TokenUsage& usage) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& mu = model_usage_[model];
    mu.input_tokens += usage.input_tokens;
    mu.output_tokens += usage.output_tokens;
    mu.cache_creation_input_tokens += usage.cache_creation_input_tokens;
    mu.cache_read_input_tokens += usage.cache_read_input_tokens;
    total_input_tokens_ += usage.input_tokens;
    total_output_tokens_ += usage.output_tokens;
    total_cache_creation_tokens_ += usage.cache_creation_input_tokens;
    total_cache_read_tokens_ += usage.cache_read_input_tokens;
    api_call_count_++;
}

void CostTracker::record_api_duration(double duration_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    total_api_duration_ms_ += duration_ms;
    ++api_call_count_;
}

void CostTracker::record_tool_duration(double duration_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    total_tool_duration_ms_ += duration_ms;
}

TokenUsage CostTracker::total_usage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    TokenUsage u;
    u.input_tokens = total_input_tokens_;
    u.output_tokens = total_output_tokens_;
    u.cache_creation_input_tokens = total_cache_creation_tokens_;
    u.cache_read_input_tokens = total_cache_read_tokens_;
    return u;
}

double CostTracker::total_cost_usd() const {
    return total_usage().estimate_cost_usd();
}

std::unordered_map<std::string, TokenUsage> CostTracker::usage_by_model() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return model_usage_;
}

void CostTracker::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    total_input_tokens_ = 0;
    total_output_tokens_ = 0;
    total_cache_creation_tokens_ = 0;
    total_cache_read_tokens_ = 0;
    model_usage_.clear();
    total_api_duration_ms_ = 0.0;
    total_tool_duration_ms_ = 0.0;
    api_call_count_ = 0;
    tool_call_count_ = 0;
}

std::string CostTracker::format_cost() const {
    std::lock_guard<std::mutex> lock(mutex_);
    double cost = total_usage().estimate_cost_usd();
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);
    if (cost > 0.5) {
        oss << "$" << std::setprecision(2) << cost;
    } else {
        oss << "$" << cost;
    }
    return oss.str();
}

std::string CostTracker::format_usage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;
    oss << "Usage: " << total_input_tokens_ << " input, "
        << total_output_tokens_ << " output, "
        << total_cache_read_tokens_ << " cache read, "
        << total_cache_creation_tokens_ << " cache write";

    if (!model_usage_.empty()) {
        oss << "\n\nUsage by model:";
        for (const auto& [model, usage] : model_usage_) {
            oss << "\n  " << model << ": "
                << usage.input_tokens << " input, "
                << usage.output_tokens << " output, "
                << usage.cache_read_input_tokens << " cache read, "
                << usage.cache_creation_input_tokens << " cache write";
        }
    }
    return oss.str();
}

}  // namespace claude
