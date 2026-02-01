#!/bin/bash
# Wrapper to run the classic-mac-hardware MCP server inside the Docker container
# This allows the MCP server to run without installing Python/MCP on the host

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
CONTAINER_NAME="peertalk-dev"
IMAGE_NAME="mndeaves/peertalk:latest"

# Check if container exists
if ! docker ps -a --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
    # Container doesn't exist - create it
    docker run -d \
        --name "$CONTAINER_NAME" \
        --network host \
        -v "$PROJECT_DIR:/workspace" \
        "$IMAGE_NAME" \
        sleep infinity
fi

# Check if container is running
if ! docker ps --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
    # Container exists but stopped - start it
    docker start "$CONTAINER_NAME" >/dev/null
fi

# Run the MCP server inside the container
# Use exec -i to pass stdin/stdout for MCP protocol
# Change to project directory first to avoid mount namespace issues
cd "$PROJECT_DIR" || exit 1
exec docker exec -i -w /workspace "$CONTAINER_NAME" \
    python3 /workspace/.claude/mcp-servers/classic-mac-hardware/server.py
