# PeerTalk Makefile
# POSIX build system

CC = gcc
CFLAGS = -Wall -Werror -std=c99 -g -O2 -I./include
LDFLAGS = -lpthread

# PT_Log library
LOG_SRCS = src/log/pt_log_posix.c
LOG_OBJS = $(LOG_SRCS:.c=.o)

all: libptlog.a

libptlog.a: $(LOG_OBJS)
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

# Clean
clean:
	rm -f $(LOG_OBJS) libptlog.a test_log test_log_perf
	rm -f src/log/*.o

.PHONY: all test-log valgrind clean
