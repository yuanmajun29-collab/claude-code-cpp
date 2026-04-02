#pragma once

#include "tools/tool_base.h"

namespace claude {

// File reading tool with image support
class FileReadTool : public ToolBase {
public:
    FileReadTool() = default;
    ~FileReadTool() override = default;

    std::string name() const override { return "Read"; }
    std::string description() const override {
        return "Read the contents of a file. Supports text files with optional "
               "offset and line limiting, image files (base64), and binary detection.";
    }

    ToolInputSchema input_schema() const override;
    std::string system_prompt() const override;
    ToolOutput execute(const std::string& input_json, ToolContext& ctx) override;

    std::string category() const override { return "file"; }

private:
    // Detect MIME type from file extension and magic bytes
    std::string detect_mime_type(const std::string& path) const;

    // Read image file as base64
    ToolOutput read_image(const std::string& path) const;

    // Read text file with line numbers
    ToolOutput read_text(const std::string& path, int offset, int limit) const;

    // Read binary file info
    ToolOutput read_binary(const std::string& path) const;
};

}  // namespace claude
