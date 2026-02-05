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

# Test executables
TEST_BINS = $(BIN_DIR)/test_log $(BIN_DIR)/test_log_perf $(BIN_DIR)/test_log_threads \
            $(BIN_DIR)/test_compat $(BIN_DIR)/test_foundation $(BIN_DIR)/test_protocol \
            $(BIN_DIR)/test_peer $(BIN_DIR)/test_queue $(BIN_DIR)/test_queue_advanced \
            $(BIN_DIR)/test_backpressure $(BIN_DIR)/test_discovery_posix \
            $(BIN_DIR)/test_messaging_posix $(BIN_DIR)/test_udp_posix $(BIN_DIR)/test_stats_posix \
            $(BIN_DIR)/test_integration_posix

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

$(BIN_DIR)/test_sendex: tests/test_sendex.c $(LIBPEERTALK) $(LIBPTLOG) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIB_DIR) -lpeertalk -lptlog $(LDFLAGS)

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
test-local: test-log test-compat test-foundation test-protocol test-peer test-queue test-queue-advanced test-backpressure test-discovery test-messaging test-udp test-stats test-integration-posix test-sendex
	@echo ""
	@echo "All tests passed!"

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
	        $(BIN_DIR)/test_sendex
	$(MAKE) test-local
	lcov --capture --directory $(OBJ_DIR) --output-file $(COV_DIR)/coverage.info
	lcov --remove $(COV_DIR)/coverage.info '/usr/*' --ignore-errors unused --output-file $(COV_DIR)/coverage.info
	genhtml $(COV_DIR)/coverage.info --output-directory $(COV_DIR)/html
	@echo "Coverage report: $(COV_DIR)/html/index.html"

# Default coverage target uses Docker
coverage: docker-coverage

# Docker Integration Test (Session 4.6)
test-integration-docker:
	@echo "=== Running 3-Peer Docker Integration Test ==="
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
        docker-build docker-test docker-coverage \
        valgrind coverage coverage-local clean
