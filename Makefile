# PeerTalk Makefile
# POSIX build system

CC = gcc
CFLAGS = -Wall -Werror -std=c99 -g -O2 -I./include -I./src/core
CFLAGS += -DPT_LOG_ENABLED -DPT_PLATFORM_POSIX -D_POSIX_C_SOURCE=199309L
LDFLAGS = -lpthread

# PeerTalk core library (added incrementally as sessions complete)
CORE_SRCS = src/core/pt_version.c src/core/pt_compat.c src/core/pt_init.c src/core/protocol.c src/core/peer.c src/core/queue.c src/core/send.c
POSIX_SRCS = src/posix/platform_posix.c src/posix/net_posix.c

PEERTALK_SRCS = $(CORE_SRCS) $(POSIX_SRCS)
PEERTALK_OBJS = $(PEERTALK_SRCS:.c=.o)

# PT_Log library
LOG_SRCS = src/log/pt_log_posix.c
LOG_OBJS = $(LOG_SRCS:.c=.o)

all: libptlog.a libpeertalk.a

libptlog.a: $(LOG_OBJS)
	ar rcs $@ $^

libpeertalk.a: $(PEERTALK_OBJS)
	@mkdir -p $(@D)
	ar rcs $@ $^

# PT_Log tests
test_log: tests/test_log_posix.c libptlog.a
	$(CC) $(CFLAGS) -o $@ $< -L. -lptlog $(LDFLAGS)

test_log_perf: tests/test_log_perf.c libptlog.a
	$(CC) $(CFLAGS) -o $@ $< -L. -lptlog $(LDFLAGS)

test_log_threads: tests/test_log_threads.c libptlog.a
	$(CC) $(CFLAGS) -o $@ $< -L. -lptlog $(LDFLAGS)

test_compat: tests/test_compat.c libpeertalk.a
	$(CC) $(CFLAGS) -o $@ $< -L. -lpeertalk $(LDFLAGS)

test_foundation: tests/test_foundation.c libpeertalk.a libptlog.a
	$(CC) $(CFLAGS) -o $@ $< -L. -lpeertalk -lptlog $(LDFLAGS)

test_protocol: tests/test_protocol.c libpeertalk.a libptlog.a
	$(CC) $(CFLAGS) -o $@ $< -L. -lpeertalk -lptlog $(LDFLAGS)

test_peer: tests/test_peer.c libpeertalk.a libptlog.a
	$(CC) $(CFLAGS) -o $@ $< -L. -lpeertalk -lptlog $(LDFLAGS)

test_queue: tests/test_queue.c libpeertalk.a libptlog.a
	$(CC) $(CFLAGS) -o $@ $< -L. -lpeertalk -lptlog $(LDFLAGS)

test_queue_advanced: tests/test_queue_advanced.c libpeertalk.a libptlog.a
	$(CC) $(CFLAGS) -o $@ $< -L. -lpeertalk -lptlog $(LDFLAGS)

test_backpressure: tests/test_backpressure.c libpeertalk.a libptlog.a
	$(CC) $(CFLAGS) -o $@ $< -L. -lpeertalk -lptlog $(LDFLAGS)

test_discovery_posix: tests/test_discovery_posix.c libpeertalk.a libptlog.a
	$(CC) $(CFLAGS) -I./src/posix -o $@ $< -L. -lpeertalk -lptlog $(LDFLAGS)

test-log: test_log test_log_perf test_log_threads
	@echo "Running PT_Log tests..."
	./test_log
	./test_log_perf
	./test_log_threads

test-compat: test_compat
	@echo "Running pt_compat tests..."
	./test_compat

test-foundation: test_foundation
	@echo "Running foundation integration tests..."
	./test_foundation

test-protocol: test_protocol
	@echo "Running protocol tests..."
	./test_protocol

test-peer: test_peer
	@echo "Running peer management tests..."
	./test_peer

test-queue: test_queue
	@echo "Running message queue tests..."
	./test_queue

test-queue-advanced: test_queue_advanced
	@echo "Running Phase 3 advanced queue tests..."
	./test_queue_advanced

test-backpressure: test_backpressure
	@echo "Running Phase 3 backpressure tests..."
	./test_backpressure

test-discovery: test_discovery_posix
	@echo "Running Phase 4.1 discovery test..."
	@echo "NOTE: This is an interactive test. Run manually:"
	@echo "  Terminal 1: ./test_discovery_posix Alice"
	@echo "  Terminal 2: ./test_discovery_posix Bob"

# Valgrind memory check (MEDIUM PRIORITY - comprehensive leak detection)
valgrind: test_log test_log_perf test_log_threads test_compat test_foundation test_protocol test_peer test_queue test_queue_advanced test_backpressure
	@echo "=== Valgrind Memory Leak Detection ==="
	@echo "Running all tests through valgrind..."
	@echo ""
	@echo "PT_Log tests..."
	valgrind --leak-check=full --error-exitcode=1 --quiet ./test_log
	valgrind --leak-check=full --error-exitcode=1 --quiet ./test_log_perf
	valgrind --leak-check=full --error-exitcode=1 --quiet ./test_log_threads
	@echo ""
	@echo "Compat tests..."
	valgrind --leak-check=full --error-exitcode=1 --quiet ./test_compat
	@echo ""
	@echo "Foundation tests..."
	valgrind --leak-check=full --error-exitcode=1 --quiet ./test_foundation
	@echo ""
	@echo "Protocol tests..."
	valgrind --leak-check=full --error-exitcode=1 --quiet ./test_protocol
	@echo ""
	@echo "Peer tests..."
	valgrind --leak-check=full --error-exitcode=1 --quiet ./test_peer
	@echo ""
	@echo "Queue tests..."
	valgrind --leak-check=full --error-exitcode=1 --quiet ./test_queue
	valgrind --leak-check=full --error-exitcode=1 --quiet ./test_queue_advanced
	valgrind --leak-check=full --error-exitcode=1 --quiet ./test_backpressure
	@echo ""
	@echo "=== All valgrind checks PASSED ==="

# Test target (runs all tests)
test: test-log test-compat test-foundation test-protocol test-peer test-queue test-queue-advanced test-backpressure
	@echo ""
	@echo "All tests passed!"

# Coverage target
# Rebuilds with coverage instrumentation, runs tests, generates HTML report
coverage:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CFLAGS) -O0 -g --coverage" LDFLAGS="$(LDFLAGS) --coverage" all test_log test_log_perf test_log_threads test_compat test_foundation test_protocol test_peer test_queue test_queue_advanced test_backpressure
	$(MAKE) test
	lcov --capture --directory . --output-file coverage.info
	lcov --remove coverage.info '/usr/*' --output-file coverage.info
	genhtml coverage.info --output-directory coverage_html
	@echo "Coverage report: coverage_html/index.html"

# Clean
clean:
	rm -f $(LOG_OBJS) $(PEERTALK_OBJS) libptlog.a libpeertalk.a
	rm -f test_log test_log_perf test_log_threads test_compat test_foundation test_protocol test_peer test_queue test_queue_advanced test_backpressure
	rm -f src/log/*.o src/core/*.o src/posix/*.o
	find . -name "*.o" -delete

.PHONY: all test test-log test-compat test-foundation valgrind coverage clean
