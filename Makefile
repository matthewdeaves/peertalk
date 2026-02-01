# PeerTalk Makefile
# POSIX build system

CC = gcc
CFLAGS = -Wall -Werror -std=c99 -g -O2 -I./include -I./src/core
CFLAGS += -DPT_LOG_ENABLED -DPT_PLATFORM_POSIX
LDFLAGS = -lpthread

# PeerTalk core library (added incrementally as sessions complete)
CORE_SRCS = src/core/pt_version.c src/core/pt_compat.c src/core/pt_init.c src/core/protocol.c src/core/peer.c src/core/queue.c
POSIX_SRCS = src/posix/platform_posix.c

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

test-log: test_log test_log_perf
	@echo "Running PT_Log tests..."
	./test_log
	./test_log_perf

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

# Valgrind memory check
valgrind: test_log
	@echo "Running valgrind memory check..."
	valgrind --leak-check=full --error-exitcode=1 ./test_log

# Test target (runs all tests)
test: test-log test-compat test-foundation test-protocol test-peer test-queue
	@echo ""
	@echo "All tests passed!"

# Clean
clean:
	rm -f $(LOG_OBJS) $(PEERTALK_OBJS) libptlog.a libpeertalk.a
	rm -f test_log test_log_perf test_compat test_foundation test_protocol test_peer test_queue
	rm -f src/log/*.o src/core/*.o src/posix/*.o
	find . -name "*.o" -delete

.PHONY: all test test-log test-compat test-foundation valgrind clean
