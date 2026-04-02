# ============================================================
# Stage 1: Build
# ============================================================
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8

# System dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    g++-12 \
    libcurl4-openssl-dev \
    libssl-dev \
    git \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Create build user
RUN useradd -m -s /bin/bash builder

WORKDIR /src

# Copy source
COPY --chown=builder:builder . .

# Build
RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=OFF \
    -DBUILD_BENCHMARKS=OFF \
    -DENABLE_MCP=ON \
    -DENABLE_LSP=ON \
    -DENABLE_BRIDGE=ON \
    -DENABLE_AGENT=ON \
    -DENABLE_PLUGINS=ON \
    -DENABLE_SANDBOX=ON \
    && cmake --build build -j$(nproc)

# ============================================================
# Stage 2: Runtime
# ============================================================
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8

# Runtime dependencies (match build-time libraries)
RUN apt-get update && apt-get install -y --no-install-recommends \
    libcurl4 \
    openssl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user
RUN groupadd -r claude && useradd -r -g claude -m -s /bin/bash claude

# Create directories
RUN mkdir -p /home/claude/.claude/plugins \
    /home/claude/.claude/memory \
    /home/claude/workspace \
    /opt/claude-code

# Copy binary
COPY --from=builder /src/build/claude-code /opt/claude-code/claude-code
RUN chmod +x /opt/claude-code/claude-code

# Copy scripts
COPY docker/entrypoint.sh /opt/claude-code/entrypoint.sh
COPY docker/healthcheck.sh /opt/claude-code/healthcheck.sh
RUN chmod +x /opt/claude-code/entrypoint.sh /opt/claude-code/healthcheck.sh

# Set ownership
RUN chown -R claude:claude /home/claude /opt/claude-code

# Environment
ENV PATH="/opt/claude-code:${PATH}"
ENV CLAUDE_HOME="/home/claude"
ENV CLAUDE_WORKSPACE="/home/claude/workspace"
ENV CLAUDE_CONFIG_DIR="/home/claude/.claude"
ENV CLAUDE_PLUGIN_PATH="/home/claude/.claude/plugins"

WORKDIR ${CLAUDE_WORKSPACE}

USER claude

EXPOSE 9876

HEALTHCHECK --interval=30s --timeout=5s --retries=3 --start-period=5s \
    CMD ["/opt/claude-code/healthcheck.sh"]

ENTRYPOINT ["/opt/claude-code/entrypoint.sh"]
CMD ["--help"]
