#!/bin/bash
# ============================================================
# Claude Code C++ — Docker Entrypoint
# ============================================================
set -euo pipefail

# Color helpers (disable if NO_COLOR is set)
if [ -n "${NO_COLOR:-}" ]; then
    R="" G="" Y="" B="" N=""
else
    R="\033[0;31m" G="\033[0;32m" Y="\033[1;33m" B="\033[0;34m" N="\033[0m"
fi

info()  { echo -e "${B}[INFO]${N}  $*"; }
warn()  { echo -e "${Y}[WARN]${N}  $*"; }
error() { echo -e "${R}[ERROR]${N} $*" >&2; }

# ============================================================
# 1. Environment setup
# ============================================================
info "Claude Code C++ v0.1.0 starting..."

# Ensure API key is available
if [ -z "${ANTHROPIC_API_KEY:-}" ]; then
    # Try to load from config file
    if [ -f "${CLAUDE_CONFIG_DIR}/config.json" ]; then
        export ANTHROPIC_API_KEY=$(python3 -c "
import json, sys
try:
    with open('${CLAUDE_CONFIG_DIR}/config.json') as f:
        cfg = json.load(f)
    print(cfg.get('api_key', ''))
except: print('')
" 2>/dev/null || true)
    fi

    if [ -z "${ANTHROPIC_API_KEY:-}" ]; then
        error "ANTHROPIC_API_KEY is not set."
        echo ""
        echo "Set it via:"
        echo "  docker run -e ANTHROPIC_API_KEY=sk-ant-... claude-code-cpp"
        echo ""
        echo "Or mount a config file:"
        echo "  docker run -v ~/.claude:/home/claude/.claude claude-code-cpp"
        exit 1
    fi
fi

# Model selection
MODEL="${CLAUDE_MODEL:-claude-sonnet-4-20250514}"

# Permission mode
PERMISSION="${CLAUDE_PERMISSION_MODE:-default}"

# Bridge port (0 = auto, or specific port)
BRIDGE_PORT="${BRIDGE_PORT:-0}"

# ============================================================
# 2. Workspace initialization
# ============================================================
WORKSPACE="${CLAUDE_WORKSPACE:-/home/claude/workspace}"

if [ ! -d "${WORKSPACE}" ]; then
    mkdir -p "${WORKSPACE}"
    info "Created workspace: ${WORKSPACE}"
fi

cd "${WORKSPACE}"

# ============================================================
# 3. Plugin directory
# ============================================================
PLUGIN_DIR="${CLAUDE_PLUGIN_PATH:-/home/claude/.claude/plugins}"
if [ ! -d "${PLUGIN_DIR}" ]; then
    mkdir -p "${PLUGIN_DIR}"
    info "Created plugin directory: ${PLUGIN_DIR}"
fi

# ============================================================
# 4. Memory directory
# ============================================================
MEMORY_DIR="${CLAUDE_CONFIG_DIR}/memory"
if [ ! -d "${MEMORY_DIR}" ]; then
    mkdir -p "${MEMORY_DIR}"
    info "Created memory directory: ${MEMORY_DIR}"
fi

# ============================================================
# 5. Print configuration
# ============================================================
info "Configuration:"
info "  Model:          ${MODEL}"
info "  Permission:     ${PERMISSION}"
info "  Workspace:      ${WORKSPACE}"
info "  Plugins:        ${PLUGIN_DIR}"
info "  Bridge Port:    ${BRIDGE_PORT}"
info "  Memory:         ${MEMORY_DIR}"

# ============================================================
# 6. Build arguments
# ============================================================
ARGS=()

# Always pass model and permission
ARGS+=(--model "${MODEL}")
ARGS+=(--permission-mode "${PERMISSION}")

# Verbosity
if [ -n "${CLAUDE_VERBOSE:-}" ]; then
    ARGS+=(-v)
fi

# No color
if [ -n "${NO_COLOR:-}" ]; then
    ARGS+=(--no-color)
fi

# Max tokens
if [ -n "${MAX_TOKENS:-}" ]; then
    ARGS+=(--max-tokens "${MAX_TOKENS}")
fi

# Max iterations
if [ -n "${MAX_ITERATIONS:-}" ]; then
    ARGS+=(--max-iterations "${MAX_ITERATIONS}")
fi

# Thinking
if [ -n "${CLAUDE_THINKING:-}" ]; then
    ARGS+=(--thinking)
fi
if [ -n "${THINKING_BUDGET:-}" ]; then
    ARGS+=(--thinking-budget "${THINKING_BUDGET}")
fi

# Append user arguments
if [ $# -gt 0 ]; then
    ARGS+=("$@")
fi

# ============================================================
# 7. Execute
# ============================================================
info "Launching claude-code with args: ${ARGS[*]}"

exec /opt/claude-code/claude-code "${ARGS[@]}"
