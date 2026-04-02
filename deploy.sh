#!/bin/bash
# ============================================================
# Claude Code C++ — Deployment Script
# ============================================================
# Automates: build, push, deploy, verify
#
# Usage:
#   ./deploy.sh build          # Build Docker image
#   ./deploy.sh push           # Push to registry
#   ./deploy.sh up             # Start service
#   ./deploy.sh down           # Stop service
#   ./deploy.sh logs           # Tail logs
#   ./deploy.sh restart        # Restart service
#   ./deploy.sh status         # Show status
#   ./deploy.sh verify         # Health check
#   ./deploy.sh run "prompt"   # Run one-shot prompt
#   ./deploy.sh clean          # Remove containers + images
#   ./deploy.sh full           # Build + push + restart
# ============================================================
set -euo pipefail

# ============================================================
# Configuration
# ============================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

IMAGE_NAME="${IMAGE_NAME:-claude-code-cpp}"
IMAGE_TAG="${IMAGE_TAG:-latest}"
REGISTRY="${REGISTRY:-}"
CONTAINER_NAME="claude-code-cpp"

if [ -n "${REGISTRY}" ]; then
    FULL_IMAGE="${REGISTRY}/${IMAGE_NAME}:${IMAGE_TAG}"
else
    FULL_IMAGE="${IMAGE_NAME}:${IMAGE_TAG}"
fi

# Colors
R="\033[0;31m"; G="\033[0;32m"; Y="\033[1;33m"; B="\033[0;34m"; C="\033[0;36m"; N="\033[0m"
info()  { echo -e "${B}[INFO]${N}  $*"; }
ok()    { echo -e "${G}[OK]${N}    $*"; }
warn()  { echo -e "${Y}[WARN]${N}  $*"; }
error() { echo -e "${R}[ERROR]${N} $*" >&2; exit 1; }

# ============================================================
# Preflight check
# ============================================================
preflight() {
    # Check Docker
    if ! command -v docker &>/dev/null; then
        error "Docker is not installed. Install it first: https://docs.docker.com/get-docker/"
    fi

    # Check Docker daemon
    if ! docker info &>/dev/null; then
        error "Docker daemon is not running. Start it with: sudo systemctl start docker"
    fi

    # Check docker compose
    if ! docker compose version &>/dev/null; then
        warn "Docker Compose v2 not found. Falling back to 'docker-compose' v1."
        if ! command -v docker-compose &>/dev/null; then
            error "Neither docker compose v2 nor docker-compose v1 found."
        fi
    fi

    # Check .env
    if [ ! -f "${PROJECT_DIR}/.env" ]; then
        if [ -f "${PROJECT_DIR}/.env.example" ]; then
            warn ".env not found. Copying from .env.example — please fill in your API key."
            cp "${PROJECT_DIR}/.env.example" "${PROJECT_DIR}/.env"
            warn "Edit .env and set ANTHROPIC_API_KEY before running."
        else
            warn ".env not found. Set ANTHROPIC_API_KEY environment variable."
        fi
    fi

    # Check API key
    if [ -z "${ANTHROPIC_API_KEY:-}" ]; then
        if [ -f "${PROJECT_DIR}/.env" ]; then
            source "${PROJECT_DIR}/.env" 2>/dev/null || true
        fi
    fi

    if [ -z "${ANTHROPIC_API_KEY:-}" ] && [ -f "${PROJECT_DIR}/.env" ]; then
        # Try to grep from .env (don't source to avoid side effects)
        ANTHROPIC_API_KEY=$(grep '^ANTHROPIC_API_KEY=' "${PROJECT_DIR}/.env" | cut -d= -f2 | tr -d '"' | tr -d "'")
    fi

    ok "Preflight checks passed"
}

# ============================================================
# Commands
# ============================================================
cmd_build() {
    info "Building Docker image: ${FULL_IMAGE}"
    cd "${PROJECT_DIR}"
    docker build -t "${IMAGE_NAME}:${IMAGE_TAG}" .
    ok "Image built: ${FULL_IMAGE} ($(docker images -q "${IMAGE_NAME}:${IMAGE_TAG}" | head -1 | cut -c1-12)...)"

    # Show image size
    local size
    size=$(docker images "${IMAGE_NAME}:${IMAGE_TAG}" --format "{{.Size}}")
    info "Image size: ${size}"
}

cmd_push() {
    if [ -z "${REGISTRY}" ]; then
        error "REGISTRY is not set. Usage: REGISTRY=ghcr.io/yourname ./deploy.sh push"
    fi

    info "Pushing image: ${FULL_IMAGE}"
    docker push "${FULL_IMAGE}"
    ok "Push complete"
}

cmd_up() {
    preflight
    info "Starting Claude Code C++..."

    cd "${PROJECT_DIR}"

    # Ensure workspace directory exists
    mkdir -p "${WORKSPACE_DIR:-./workspace}"

    docker compose up -d claude
    ok "Service started"

    # Wait for health check
    info "Waiting for health check..."
    local retries=10
    while [ $retries -gt 0 ]; do
        if docker compose ps claude 2>/dev/null | grep -q "healthy"; then
            ok "Service is healthy"
            return 0
        fi
        sleep 2
        retries=$((retries - 1))
    done
    warn "Health check not yet passing (service may still be starting)"
}

cmd_down() {
    info "Stopping Claude Code C++..."
    cd "${PROJECT_DIR}"
    docker compose down
    ok "Service stopped"
}

cmd_restart() {
    info "Restarting Claude Code C++..."
    cd "${PROJECT_DIR}"
    docker compose restart claude
    ok "Service restarted"
}

cmd_logs() {
    cd "${PROJECT_DIR}"
    docker compose logs -f claude --tail=100
}

cmd_status() {
    cd "${PROJECT_DIR}"
    echo ""
    echo "=== Container Status ==="
    docker compose ps
    echo ""

    echo "=== Resource Usage ==="
    docker stats --no-stream "${CONTAINER_NAME}" 2>/dev/null || echo "(container not running)"

    echo ""
    echo "=== Image Info ==="
    docker images "${IMAGE_NAME}" --format "table {{.Repository}}:{{.Tag}}\t{{.Size}}\t{{.CreatedAt}}" 2>/dev/null || echo "(image not found)"
}

cmd_verify() {
    info "Running verification..."

    # 1. Container running?
    if docker ps --filter "name=${CONTAINER_NAME}" --filter "status=running" -q | grep -q .; then
        ok "Container is running"
    else
        error "Container is not running"
    fi

    # 2. Health check?
    if docker inspect --format='{{.State.Health.Status}}' "${CONTAINER_NAME}" 2>/dev/null | grep -q "healthy"; then
        ok "Health check: healthy"
    else
        warn "Health check: not healthy (may still be starting)"
    fi

    # 3. Version
    info "Claude Code version:"
    docker exec "${CONTAINER_NAME}" /opt/claude-code/claude-code -V 2>&1 | head -1

    # 4. Environment
    info "Environment check:"
    docker exec "${CONTAINER_NAME}" printenv ANTHROPIC_API_KEY 2>/dev/null | sed 's/./x/g' | head -c 12 && echo "... (API key set)" || warn "API key not set"
}

cmd_run() {
    local prompt="${1:-}"
    if [ -z "${prompt}" ]; then
        error "Usage: ./deploy.sh run \"your prompt\""
    fi

    preflight
    info "Running one-shot prompt..."
    cd "${PROJECT_DIR}"

    mkdir -p "${WORKSPACE_DIR:-./workspace}"
    docker compose run --rm prompt "${prompt}"
}

cmd_clean() {
    warn "This will remove all containers, images, and volumes."
    read -p "Are you sure? [y/N] " -n 1 -r
    echo

    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        info "Cancelled"
        return 0
    fi

    cd "${PROJECT_DIR}"
    docker compose down -v --rmi local --remove-orphans 2>/dev/null || true
    docker rmi "${IMAGE_NAME}:${IMAGE_TAG}" 2>/dev/null || true
    ok "Cleanup complete"
}

cmd_full() {
    cmd_build
    if [ -n "${REGISTRY}" ]; then
        cmd_push
    fi
    cmd_down 2>/dev/null || true
    cmd_up
    cmd_verify
}

cmd_interactive() {
    preflight
    info "Starting interactive session..."
    cd "${PROJECT_DIR}"

    mkdir -p "${WORKSPACE_DIR:-./workspace}"
    docker compose run --rm claude
}

# ============================================================
# Help
# ============================================================
usage() {
    echo ""
    echo -e "${C}Claude Code C++ — Docker Deployment${N}"
    echo ""
    echo "Usage: ./deploy.sh <command> [options]"
    echo ""
    echo "Commands:"
    echo "  build       Build Docker image"
    echo "  push        Push image to registry (requires REGISTRY)"
    echo "  up          Start service (detached)"
    echo "  down        Stop service"
    echo "  restart     Restart service"
    echo "  logs        Tail service logs"
    echo "  status      Show container and resource status"
    echo "  verify      Run health verification checks"
    echo "  run <msg>   Run a one-shot prompt"
    echo "  interactive Start interactive REPL session"
    echo "  clean       Remove containers, images, and volumes"
    echo "  full        Build + (push) + restart + verify"
    echo ""
    echo "Environment Variables:"
    echo "  IMAGE_NAME   Image name (default: claude-code-cpp)"
    echo "  IMAGE_TAG    Image tag (default: latest)"
    echo "  REGISTRY     Container registry (e.g., ghcr.io/user)"
    echo "  ANTHROPIC_API_KEY  API key (or set in .env)"
    echo ""
    echo "Examples:"
    echo "  ./deploy.sh build"
    echo "  REGISTRY=ghcr.io/yuanmajun29-collab ./deploy.sh push"
    echo "  ./deploy.sh run 'Write a Python quicksort'"
    echo "  ./deploy.sh up && ./deploy.sh logs"
    echo ""
}

# ============================================================
# Main
# ============================================================
cd "${PROJECT_DIR}"

case "${1:-help}" in
    build)      cmd_build ;;
    push)       cmd_push ;;
    up)         cmd_up ;;
    down)       cmd_down ;;
    restart)    cmd_restart ;;
    logs)       cmd_logs ;;
    status)     cmd_status ;;
    verify)     cmd_verify ;;
    run)        cmd_run "${2:-}" ;;
    interactive) cmd_interactive ;;
    clean)      cmd_clean ;;
    full)       cmd_full ;;
    help|-h|--help) usage ;;
    *)          error "Unknown command: ${1}. Run './deploy.sh help' for usage." ;;
esac
