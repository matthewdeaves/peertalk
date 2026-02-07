#!/bin/bash
# Wrapper to run the classic-mac-hardware MCP server inside the Docker container
# This allows the MCP server to run without installing Python/MCP on the host

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
CONTAINER_NAME="peertalk-dev"
IMAGE_NAME="ghcr.io/matthewdeaves/peertalk-dev:develop"

# Check if Docker is installed
if ! command -v docker &> /dev/null; then
    echo "Error: Docker not found. Please install Docker first." >&2
    echo "Visit: https://docs.docker.com/get-docker/" >&2
    exit 1
fi

# Check if Docker daemon is running
if ! docker info &> /dev/null; then
    echo "Error: Docker daemon is not running. Please start Docker." >&2
    exit 1
fi

# Check if image exists, pull if needed
if ! docker image inspect "$IMAGE_NAME" &> /dev/null; then
    echo "Image $IMAGE_NAME not found. Run ./tools/setup.sh to build it." >&2
    exit 1
fi

# Check if container exists
if ! docker ps -a --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
    # Container doesn't exist - create it
    docker run -d \
        --name "$CONTAINER_NAME" \
        --network host \
        -v "$PROJECT_DIR:/workspace" \
        "$IMAGE_NAME" \
        sleep infinity >/dev/null
fi

# Check if container is running
if ! docker ps --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
    # Container exists but stopped - start it
    docker start "$CONTAINER_NAME" >/dev/null
fi

# Wait briefly for container to be ready
sleep 0.5

# Run the MCP server inside the container
# Use exec -i to pass stdin/stdout for MCP protocol
# Change to project directory first to avoid mount namespace issues
cd "$PROJECT_DIR" || exit 1
exec docker exec -i -w /workspace "$CONTAINER_NAME" \
    python3 /workspace/.claude/mcp-servers/classic-mac-hardware/server.py
