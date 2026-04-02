#pragma once

#include <string>
#include "tools/tool_base.h"  // PermissionResult defined here

namespace claude {

enum class PermissionMode {
    Default,
    Bypass,
    Yolo,
    Plan,
    AutoEdit,
    Ask
};

}  // namespace claude
