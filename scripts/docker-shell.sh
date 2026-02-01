#!/bin/bash
# Start an interactive shell in the PeerTalk development container
#
# Usage: ./scripts/docker-shell.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_DIR"

# Run using docker-compose
if command -v docker-compose >/dev/null 2>&1; then
    docker-compose -f docker/docker-compose.yml run --rm peertalk-dev
elif command -v docker >/dev/null 2>&1; then
    docker compose -f docker/docker-compose.yml run --rm peertalk-dev
else
    echo "ERROR: Neither docker-compose nor docker compose found"
    exit 1
fi
