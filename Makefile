# PeerTalk Makefile
# POSIX build system

CC = gcc
CFLAGS = -Wall -Werror -std=c99 -g -O2 -I./include -I./src/core
CFLAGS += -DPT_LOG_ENABLED -DPT_PLATFORM_POSIX
LDFLAGS = -lpthread

# PeerTalk core library (added incrementally as sessions complete)
CORE_SRCS = src/core/pt_version.c
POSIX_SRCS =

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

test-log: test_log test_log_perf
	@echo "Running PT_Log tests..."
	./test_log
	./test_log_perf

# Valgrind memory check
valgrind: test_log
	@echo "Running valgrind memory check..."
	valgrind --leak-check=full --error-exitcode=1 ./test_log

# Test target (placeholder until Session 1.5)
test:
	@echo "No PeerTalk tests yet"

# Clean
clean:
	rm -f $(LOG_OBJS) $(PEERTALK_OBJS) libptlog.a libpeertalk.a test_log test_log_perf
	rm -f src/log/*.o src/core/*.o src/posix/*.o
	find . -name "*.o" -delete

.PHONY: all test test-log valgrind clean
