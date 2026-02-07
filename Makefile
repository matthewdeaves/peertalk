# PeerTalk Makefile
# POSIX build system

CC = gcc
CFLAGS = -Wall -Werror -std=c99 -g -O2 -I./include -I./src/core
CFLAGS += -DPT_LOG_ENABLED -DPT_PLATFORM_POSIX -D_POSIX_C_SOURCE=199309L
LDFLAGS = -lpthread

# Build directories
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
LIB_DIR = $(BUILD_DIR)/lib
BIN_DIR = $(BUILD_DIR)/bin
COV_DIR = $(BUILD_DIR)/coverage

# PeerTalk core library
CORE_SRCS = src/core/pt_version.c src/core/pt_compat.c src/core/pt_init.c src/core/protocol.c src/core/peer.c src/core/queue.c src/core/send.c
POSIX_SRCS = src/posix/platform_posix.c src/posix/net_posix.c
PEERTALK_SRCS = $(CORE_SRCS) $(POSIX_SRCS)
PEERTALK_OBJS = $(PEERTALK_SRCS:%.c=$(OBJ_DIR)/%.o)

# PT_Log library
LOG_SRCS = src/log/pt_log_posix.c
LOG_OBJS = $(LOG_SRCS:%.c=$(OBJ_DIR)/%.o)

# Libraries
LIBPTLOG = $(LIB_DIR)/libptlog.a
LIBPEERTALK = $(LIB_DIR)/libpeertalk.a

# Test executables (unit tests that run in single container)
TEST_BINS = $(BIN_DIR)/test_log $(BIN_DIR)/test_log_perf $(BIN_DIR)/test_log_threads \
            $(BIN_DIR)/test_compat $(BIN_DIR)/test_foundation $(BIN_DIR)/test_protocol \
            $(BIN_DIR)/test_peer $(BIN_DIR)/test_queue $(BIN_DIR)/test_queue_advanced \
            $(BIN_DIR)/test_backpressure $(BIN_DIR)/test_discovery_posix \
            $(BIN_DIR)/test_messaging_posix $(BIN_DIR)/test_udp_posix $(BIN_DIR)/test_stats_posix \
            $(BIN_DIR)/test_integration_posix $(BIN_DIR)/test_sendex \
            $(BIN_DIR)/test_api_errors $(BIN_DIR)/test_queue_extended \
            $(BIN_DIR)/test_batch_send $(BIN_DIR)/test_connection \
            $(BIN_DIR)/test_loopback_messaging $(BIN_DIR)/test_protocol_messaging \
            $(BIN_DIR)/test_bidirectional $(BIN_DIR)/test_tcp_send_recv \
            $(BIN_DIR)/test_discovery_recv $(BIN_DIR)/test_error_strings \
            $(BIN_DIR)/test_perf_benchmarks $(BIN_DIR)/test_protocol_fuzz \
            $(BIN_DIR)/test_queue_threads

# Unity-based tests (optional - requires Unity framework)
TEST_UNITY_BINS = $(BIN_DIR)/test_queue_unity

# Integration test (requires docker-compose with multiple peers)
INTEGRATION_TEST_BIN = $(BIN_DIR)/test_integration_full

all: $(LIBPTLOG) $(LIBPEERTALK)

# Create build directories
$(BUILD_DIR) $(OBJ_DIR) $(LIB_DIR) $(BIN_DIR) $(COV_DIR):
	@mkdir -p $@

# Object file compilation (with automatic dependency on directory creation)
$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Libraries
$(LIBPTLOG): $(LOG_OBJS) | $(LIB_DIR)
	ar rcs $@ $^

$(LIBPEERTALK): $(PEERTALK_OBJS) | $(LIB_DIR)
	ar rcs $@ $^

# PT_Log tests
$(BIN_DIR)/test_log: tests/test_log_posix.c $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lptlog $(LDFLAGS)

$(BIN_DIR)/test_log_perf: tests/test_log_perf.c $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lptlog $(LDFLAGS)

$(BIN_DIR)/test_log_threads: tests/test_log_threads.c $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lptlog $(LDFLAGS)

# PeerTalk tests
$(BIN_DIR)/test_compat: tests/test_compat.c $(LIBPEERTALK) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lpeertalk $(LDFLAGS)

$(BIN_DIR)/test_foundation: tests/test_foundation.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_protocol: tests/test_protocol.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_peer: tests/test_peer.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_queue: tests/test_queue.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_queue_advanced: tests/test_queue_advanced.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_backpressure: tests/test_backpressure.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_discovery_posix: tests/test_discovery_posix.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -I./src/posix -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_messaging_posix: tests/test_messaging_posix.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_udp_posix: tests/test_udp_posix.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_stats_posix: tests/test_stats_posix.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_integration_posix: tests/test_integration_posix.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_integration_full: tests/test_integration_full.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_sendex: tests/test_sendex.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_api_errors: tests/test_api_errors.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_queue_extended: tests/test_queue_extended.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_batch_send: tests/test_batch_send.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_connection: tests/test_connection.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_loopback_messaging: tests/test_loopback_messaging.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -I./src/posix -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_protocol_messaging: tests/test_protocol_messaging.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -I./src/posix -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_bidirectional: tests/test_bidirectional.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -I./src/posix -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_tcp_send_recv: tests/test_tcp_send_recv.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -I./src/posix -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_discovery_recv: tests/test_discovery_recv.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -I./src/posix -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_error_strings: tests/test_error_strings.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_perf_benchmarks: tests/test_perf_benchmarks.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_protocol_fuzz: tests/test_protocol_fuzz.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

$(BIN_DIR)/test_queue_threads: tests/test_queue_threads.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

# Unity-based tests
$(BIN_DIR)/test_queue_unity: tests/test_queue_unity.c tests/unity/unity.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ tests/test_queue_unity.c tests/unity/unity.c -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

# Test runners
test-log: $(BIN_DIR)/test_log $(BIN_DIR)/test_log_perf $(BIN_DIR)/test_log_threads
	@echo "Running PT_Log tests..."
	@$(BIN_DIR)/test_log
	@$(BIN_DIR)/test_log_perf
	@$(BIN_DIR)/test_log_threads

test-compat: $(BIN_DIR)/test_compat
	@echo "Running pt_compat tests..."
	@$(BIN_DIR)/test_compat

test-foundation: $(BIN_DIR)/test_foundation
	@echo "Running foundation integration tests..."
	@$(BIN_DIR)/test_foundation

test-protocol: $(BIN_DIR)/test_protocol
	@echo "Running protocol tests..."
	@$(BIN_DIR)/test_protocol

test-peer: $(BIN_DIR)/test_peer
	@echo "Running peer management tests..."
	@$(BIN_DIR)/test_peer

test-queue: $(BIN_DIR)/test_queue
	@echo "Running message queue tests..."
	@$(BIN_DIR)/test_queue

test-queue-advanced: $(BIN_DIR)/test_queue_advanced
	@echo "Running Phase 3 advanced queue tests..."
	@$(BIN_DIR)/test_queue_advanced

test-backpressure: $(BIN_DIR)/test_backpressure
	@echo "Running Phase 3 backpressure tests..."
	@$(BIN_DIR)/test_backpressure

test-discovery: $(BIN_DIR)/test_discovery_posix
	@echo "Running Phase 4.1 discovery test (automated, 30s)..."
	@$(BIN_DIR)/test_discovery_posix AutoTest

test-integration-posix: $(BIN_DIR)/test_integration_posix
	@echo "Running Phase 4.6 poll loop integration test..."
	@$(BIN_DIR)/test_integration_posix

test-messaging: $(BIN_DIR)/test_messaging_posix
	@echo "Running Phase 4.3 message I/O test..."
	@$(BIN_DIR)/test_messaging_posix

test-udp: $(BIN_DIR)/test_udp_posix
	@echo "Running Phase 4.4 UDP messaging test..."
	@$(BIN_DIR)/test_udp_posix

test-stats: $(BIN_DIR)/test_stats_posix
	@echo "Running Phase 4.5 network statistics test..."
	@$(BIN_DIR)/test_stats_posix

test-sendex: $(BIN_DIR)/test_sendex
	@echo "Running Phase 3.5 SendEx API tests..."
	@$(BIN_DIR)/test_sendex

test-queue-threads: $(BIN_DIR)/test_queue_threads
	@echo "Running queue thread safety tests..."
	@$(BIN_DIR)/test_queue_threads

test-unity: $(BIN_DIR)/test_queue_unity
	@echo "Running Unity-based queue tests..."
	@$(BIN_DIR)/test_queue_unity

# Valgrind memory check
valgrind: $(BIN_DIR)/test_log $(BIN_DIR)/test_log_perf $(BIN_DIR)/test_log_threads \
          $(BIN_DIR)/test_compat $(BIN_DIR)/test_foundation $(BIN_DIR)/test_protocol \
          $(BIN_DIR)/test_peer $(BIN_DIR)/test_queue $(BIN_DIR)/test_queue_advanced \
          $(BIN_DIR)/test_backpressure
	@echo "=== Valgrind Memory Leak Detection ==="
	@echo "Running all tests through valgrind..."
	@echo ""
	@echo "PT_Log tests..."
	@valgrind --leak-check=full --error-exitcode=1 --quiet $(BIN_DIR)/test_log
	@valgrind --leak-check=full --error-exitcode=1 --quiet $(BIN_DIR)/test_log_perf
	@valgrind --leak-check=full --error-exitcode=1 --quiet $(BIN_DIR)/test_log_threads
	@echo ""
	@echo "Compat tests..."
	@valgrind --leak-check=full --error-exitcode=1 --quiet $(BIN_DIR)/test_compat
	@echo ""
	@echo "Foundation tests..."
	@valgrind --leak-check=full --error-exitcode=1 --quiet $(BIN_DIR)/test_foundation
	@echo ""
	@echo "Protocol tests..."
	@valgrind --leak-check=full --error-exitcode=1 --quiet $(BIN_DIR)/test_protocol
	@echo ""
	@echo "Peer tests..."
	@valgrind --leak-check=full --error-exitcode=1 --quiet $(BIN_DIR)/test_peer
	@echo ""
	@echo "Queue tests..."
	@valgrind --leak-check=full --error-exitcode=1 --quiet $(BIN_DIR)/test_queue
	@valgrind --leak-check=full --error-exitcode=1 --quiet $(BIN_DIR)/test_queue_advanced
	@valgrind --leak-check=full --error-exitcode=1 --quiet $(BIN_DIR)/test_backpressure
	@echo ""
	@echo "=== All valgrind checks PASSED ==="

# Docker targets (run in container)
docker-build:
	@echo "Building in Docker container..."
	@docker run --rm -v $(PWD):/workspace -w /workspace peertalk-dev make all

docker-test:
	@echo "Running tests in Docker container..."
	@docker run --rm -v $(PWD):/workspace -w /workspace peertalk-dev bash -c "make clean && make all && make test-local"

docker-coverage:
	@echo "Running coverage in Docker container..."
	@docker run --rm -v $(PWD):/workspace -w /workspace peertalk-dev bash -c "make coverage-local"

# Local targets (run inside container or on host with dependencies)
test-local: test-log test-compat test-foundation test-protocol test-peer test-queue test-queue-advanced test-backpressure test-discovery test-messaging test-udp test-stats test-integration-posix test-sendex test-api-errors test-queue-extended test-batch-send test-connection test-loopback-messaging test-protocol-messaging test-bidirectional test-tcp-send-recv test-discovery-recv test-error-strings test-fuzz test-queue-threads
	@echo ""
	@echo "All tests passed!"

test-api-errors: $(BIN_DIR)/test_api_errors
	@echo "Running API error tests..."
	@$(BIN_DIR)/test_api_errors

test-queue-extended: $(BIN_DIR)/test_queue_extended
	@echo "Running extended queue tests..."
	@$(BIN_DIR)/test_queue_extended

test-batch-send: $(BIN_DIR)/test_batch_send
	@echo "Running batch send tests..."
	@$(BIN_DIR)/test_batch_send

test-connection: $(BIN_DIR)/test_connection
	@echo "Running connection lifecycle tests..."
	@$(BIN_DIR)/test_connection

test-loopback-messaging: $(BIN_DIR)/test_loopback_messaging
	@echo "Running loopback messaging tests..."
	@$(BIN_DIR)/test_loopback_messaging

test-protocol-messaging: $(BIN_DIR)/test_protocol_messaging
	@echo "Running protocol messaging tests..."
	@$(BIN_DIR)/test_protocol_messaging

test-bidirectional: $(BIN_DIR)/test_bidirectional
	@echo "Running bidirectional messaging tests..."
	@$(BIN_DIR)/test_bidirectional

test-tcp-send-recv: $(BIN_DIR)/test_tcp_send_recv
	@echo "Running TCP send/receive tests..."
	@$(BIN_DIR)/test_tcp_send_recv

test-discovery-recv: $(BIN_DIR)/test_discovery_recv
	@echo "Running discovery receive tests..."
	@$(BIN_DIR)/test_discovery_recv

test-error-strings: $(BIN_DIR)/test_error_strings
	@echo "Running error string tests..."
	@$(BIN_DIR)/test_error_strings

test-benchmarks: $(BIN_DIR)/test_perf_benchmarks
	@echo "Running performance benchmarks..."
	@$(BIN_DIR)/test_perf_benchmarks

test-fuzz: $(BIN_DIR)/test_protocol_fuzz
	@echo "Running protocol fuzz tests..."
	@$(BIN_DIR)/test_protocol_fuzz

# Default test target uses Docker
test: docker-test

# Coverage target (local - runs inside container)
coverage-local:
	$(MAKE) clean
	@mkdir -p $(COV_DIR)
	$(MAKE) CFLAGS="$(CFLAGS) -O0 -g --coverage" LDFLAGS="$(LDFLAGS) --coverage" all \
	        $(BIN_DIR)/test_log $(BIN_DIR)/test_log_perf $(BIN_DIR)/test_log_threads \
	        $(BIN_DIR)/test_compat $(BIN_DIR)/test_foundation $(BIN_DIR)/test_protocol \
	        $(BIN_DIR)/test_peer $(BIN_DIR)/test_queue $(BIN_DIR)/test_queue_advanced \
	        $(BIN_DIR)/test_backpressure $(BIN_DIR)/test_discovery_posix \
	        $(BIN_DIR)/test_messaging_posix $(BIN_DIR)/test_udp_posix \
	        $(BIN_DIR)/test_stats_posix $(BIN_DIR)/test_integration_posix \
	        $(BIN_DIR)/test_sendex $(BIN_DIR)/test_api_errors \
	        $(BIN_DIR)/test_queue_extended $(BIN_DIR)/test_batch_send \
	        $(BIN_DIR)/test_connection $(BIN_DIR)/test_loopback_messaging \
	        $(BIN_DIR)/test_protocol_messaging $(BIN_DIR)/test_bidirectional \
	        $(BIN_DIR)/test_tcp_send_recv $(BIN_DIR)/test_discovery_recv \
	        $(BIN_DIR)/test_error_strings $(BIN_DIR)/test_perf_benchmarks \
	        $(BIN_DIR)/test_protocol_fuzz
	$(MAKE) test-local
	lcov --capture --directory $(OBJ_DIR) --output-file $(COV_DIR)/coverage.info
	lcov --remove $(COV_DIR)/coverage.info '/usr/*' --ignore-errors unused --output-file $(COV_DIR)/coverage.info
	genhtml $(COV_DIR)/coverage.info --output-directory $(COV_DIR)/html
	@echo "Coverage report: $(COV_DIR)/html/index.html"

# Default coverage target uses Docker
coverage: docker-coverage

# Multi-Peer Docker Integration Test
# Runs test_integration_full across 3 containers to test real network communication
test-integration-docker: $(BIN_DIR)/test_integration_full
	@echo "=== Running 3-Peer Docker Integration Test ==="
	@echo "This test runs test_integration_full across 3 containers:"
	@echo "  - Peer1 (Alice): sender mode"
	@echo "  - Peer2 (Bob): receiver mode"
	@echo "  - Peer3 (Charlie): both mode"
	@echo ""
	@echo "Building and starting 3 peer containers..."
	@docker compose -f docker/docker-compose.test.yml up --build --abort-on-container-exit
	@echo ""
	@echo "Cleaning up containers..."
	@docker compose -f docker/docker-compose.test.yml down

test-integration-docker-logs:
	@echo "=== Viewing Integration Test Logs ==="
	@docker compose -f docker/docker-compose.test.yml logs

test-integration-docker-clean:
	@echo "=== Cleaning up Docker Integration Test ==="
	@docker compose -f docker/docker-compose.test.yml down -v

# Static Analysis
analyze:
	@./tools/analyze.sh --all

analyze-quick:
	@./tools/analyze.sh --quick

analyze-complexity:
	@./tools/analyze.sh --complexity

analyze-cppcheck:
	@./tools/analyze.sh --cppcheck

analyze-duplicates:
	@./tools/analyze.sh --duplicates

# Docker-based analysis (ensures tools are available)
docker-analyze:
	@echo "Running static analysis in Docker..."
	@docker run --rm -v $(PWD):/workspace -w /workspace peertalk-dev ./tools/analyze.sh --all

# Clean
clean:
	rm -rf $(BUILD_DIR)
	find src -name "*.o" -delete
	find src -name "*.gcda" -delete
	find src -name "*.gcno" -delete

.PHONY: all test test-local test-log test-compat test-foundation test-protocol test-peer \
        test-queue test-queue-advanced test-backpressure test-discovery \
        test-messaging test-udp test-stats test-integration-docker \
        test-integration-docker-logs test-integration-docker-clean \
        test-api-errors test-queue-extended test-batch-send test-connection \
        test-loopback-messaging test-protocol-messaging test-bidirectional \
        test-tcp-send-recv test-discovery-recv test-error-strings test-benchmarks test-fuzz \
        test-queue-threads test-unity \
        docker-build docker-test docker-coverage docker-analyze \
        analyze analyze-quick analyze-complexity analyze-cppcheck analyze-duplicates \
        valgrind coverage coverage-local clean
