#!/bin/bash
# ============================================================
# Claude Code C++ — Docker Health Check
# ============================================================
# Returns 0 if healthy, 1 otherwise.
# Used by Docker HEALTHCHECK directive.
set -euo pipefail

BINARY="/opt/claude-code/claude-code"

if [ ! -x "${BINARY}" ]; then
    echo "Binary not found: ${BINARY}"
    exit 1
fi

# Check if process is responsive
# Since claude-code is an interactive REPL, we test the version flag
VERSION_OUTPUT=$("${BINARY}" -V 2>&1) && exit 0

exit 1
