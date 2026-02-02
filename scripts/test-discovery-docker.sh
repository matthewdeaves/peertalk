#!/bin/bash
# Test PeerTalk UDP discovery using Docker containers

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

# Commands
start_test() {
    echo "Building containers (this will compile inside Docker)..."
    docker-compose -f docker-compose.test.yml up --build -d

    echo ""
    echo "Waiting 5 seconds for discovery to occur..."
    sleep 5

    echo ""
    echo "========================================"
    echo "Alice's output:"
    echo "========================================"
    docker logs peertalk-alice 2>&1 | tail -20

    echo ""
    echo "========================================"
    echo "Bob's output:"
    echo "========================================"
    docker logs peertalk-bob 2>&1 | tail -20

    echo ""
    echo "========================================"
    echo "Charlie's output:"
    echo "========================================"
    docker logs peertalk-charlie 2>&1 | tail -20

    echo ""
    echo "Containers are still running. Use '$0 logs' to see updates."
    echo "Use '$0 stop' to stop the test."
}

stop_test() {
    echo "Stopping peer containers..."
    docker-compose -f docker-compose.test.yml down
}

show_logs() {
    local peer=${1:-all}

    if [ "$peer" = "all" ]; then
        echo "=== Alice ==="
        docker logs peertalk-alice 2>&1 | tail -30
        echo ""
        echo "=== Bob ==="
        docker logs peertalk-bob 2>&1 | tail -30
        echo ""
        echo "=== Charlie ==="
        docker logs peertalk-charlie 2>&1 | tail -30
    else
        docker logs "peertalk-$peer" 2>&1
    fi
}

follow_logs() {
    local peer=${1:-alice}
    docker logs -f "peertalk-$peer" 2>&1
}

show_status() {
    docker-compose -f docker-compose.test.yml ps
}

case "$1" in
    start)
        start_test
        ;;
    stop)
        stop_test
        ;;
    logs)
        show_logs "$2"
        ;;
    follow)
        follow_logs "$2"
        ;;
    status)
        show_status
        ;;
    *)
        echo "Usage: $0 {start|stop|status|logs [peer]|follow [peer]}"
        echo ""
        echo "Commands:"
        echo "  start           - Build and start 3 peer containers"
        echo "  stop            - Stop all containers"
        echo "  status          - Show container status"
        echo "  logs [peer]     - Show logs (alice/bob/charlie/all)"
        echo "  follow [peer]   - Follow logs for a peer (default: alice)"
        echo ""
        echo "Example:"
        echo "  $0 start        # Start the test"
        echo "  $0 logs bob     # Check Bob's logs"
        echo "  $0 follow alice # Follow Alice's output"
        echo "  $0 stop         # Stop the test"
        ;;
esac
