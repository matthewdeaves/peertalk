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
            $(BIN_DIR)/test_messaging_posix $(BIN_DIR)/test_udp_posix $(BIN_DIR)/test_stats_posix

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
	@echo "Running Phase 4.1 discovery test..."
	@echo "NOTE: This is an interactive test. Run manually:"
	@echo "  Terminal 1: $(BIN_DIR)/test_discovery_posix Alice"
	@echo "  Terminal 2: $(BIN_DIR)/test_discovery_posix Bob"

test-messaging: $(BIN_DIR)/test_messaging_posix
	@echo "Running Phase 4.3 message I/O test..."
	@$(BIN_DIR)/test_messaging_posix

test-udp: $(BIN_DIR)/test_udp_posix
	@echo "Running Phase 4.4 UDP messaging test..."
	@$(BIN_DIR)/test_udp_posix

test-stats: $(BIN_DIR)/test_stats_posix
	@echo "Running Phase 4.5 network statistics test..."
	@$(BIN_DIR)/test_stats_posix

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

# Test target (runs all tests)
test: test-log test-compat test-foundation test-protocol test-peer test-queue test-queue-advanced test-backpressure test-messaging test-udp test-stats
	@echo ""
	@echo "All tests passed!"

# Coverage target
coverage: | $(COV_DIR)
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CFLAGS) -O0 -g --coverage" LDFLAGS="$(LDFLAGS) --coverage" all \
	        $(BIN_DIR)/test_log $(BIN_DIR)/test_log_perf $(BIN_DIR)/test_log_threads \
	        $(BIN_DIR)/test_compat $(BIN_DIR)/test_foundation $(BIN_DIR)/test_protocol \
	        $(BIN_DIR)/test_peer $(BIN_DIR)/test_queue $(BIN_DIR)/test_queue_advanced \
	        $(BIN_DIR)/test_backpressure
	$(MAKE) test
	lcov --capture --directory $(OBJ_DIR) --output-file $(COV_DIR)/coverage.info
	lcov --remove $(COV_DIR)/coverage.info '/usr/*' --output-file $(COV_DIR)/coverage.info
	genhtml $(COV_DIR)/coverage.info --output-directory $(COV_DIR)/html
	@echo "Coverage report: $(COV_DIR)/html/index.html"

# Clean
clean:
	rm -rf $(BUILD_DIR)
	find src -name "*.o" -delete
	find src -name "*.gcda" -delete
	find src -name "*.gcno" -delete

.PHONY: all test test-log test-compat test-foundation test-protocol test-peer \
        test-queue test-queue-advanced test-backpressure test-discovery \
        test-messaging test-udp test-stats valgrind coverage clean
