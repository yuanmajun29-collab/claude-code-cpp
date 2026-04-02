#pragma once

#include "tools/tool_base.h"
#include <memory>
#include <mutex>

namespace claude {
namespace agent { class Coordinator; }

// Agent Tool — exposes the multi-agent system as a tool the model can invoke
class AgentTool : public ToolBase {
public:
    explicit AgentTool(agent::Coordinator& coordinator);

    std::string name() const override;
    std::string description() const override;
    ToolInputSchema input_schema() const override;
    std::string system_prompt() const override;
    ToolOutput execute(const std::string& input_json, ToolContext& ctx) override;
    std::string category() const override { return "agent"; }

private:
    agent::Coordinator& coordinator_;
};

}  // namespace claude
