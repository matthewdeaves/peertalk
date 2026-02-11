/* Feature test macros - must be before any includes */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

/**
 * @file perf_partner.c
 * @brief POSIX Performance Test Partner
 *
 * Multi-mode test partner for Mac hardware performance testing.
 * Supports latency echo, throughput streaming, and stress testing.
 *
 * Build with Docker:
 *   docker run --rm -v $(pwd):/workspace -w /workspace peertalk-posix:latest \
 *       gcc -Wall -O2 -I include -o build/bin/perf_partner \
 *       tests/posix/perf_partner.c -L build/lib -lpeertalk_posix
 *
 * Usage:
 *   ./perf_partner [OPTIONS]
 *
 * Options:
 *   --mode MODE     Test mode: echo (default), stream, stress
 *   --port PORT     Discovery port (default: 7353)
 *   --connect IP    Auto-connect to specified IP
 *   --size BYTES    Message size for streaming (default: 1024)
 *   --count N       Number of messages/iterations (default: 1000)
 *   --duration SEC  Test duration in seconds (default: 60)
 *   --verbose       Enable verbose logging
 *   --help          Show help
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/stat.h>

#include "peertalk.h"

/* ========================================================================== */
/* Test Configuration                                                          */
/* ========================================================================== */

typedef enum {
    MODE_ECHO,      /* Echo all received messages back (for latency tests) */
    MODE_STREAM,    /* Stream data to connected peer (for throughput tests) */
    MODE_STRESS,    /* Rapid connect/disconnect cycles */
    MODE_DISCOVERY  /* Just count discovery packets */
} TestMode;

typedef struct {
    TestMode        mode;
    int             discovery_port;
    int             tcp_port;
    const char     *connect_ip;
    int             message_size;
    int             message_count;
    int             duration_sec;
    int             verbose;
    int             fast;           /* No sleep in main loop (high CPU, low latency) */
} TestConfig;

typedef struct {
    /* Latency stats (echo mode) */
    uint64_t        echo_count;
    uint64_t        echo_bytes;
    uint64_t        echo_retries;       /* Echoes retried due to backpressure */
    uint64_t        echo_drops;         /* Echoes dropped (queue full) */
    uint64_t        min_rtt_us;
    uint64_t        max_rtt_us;
    uint64_t        total_rtt_us;

    /* Throughput stats (stream mode) */
    uint64_t        bytes_sent;
    uint64_t        bytes_received;
    uint64_t        messages_sent;
    uint64_t        messages_received;
    struct timeval  stream_start;
    struct timeval  stream_end;

    /* Discovery stats */
    uint64_t        discovery_packets_seen;
    uint64_t        unique_peers_found;

    /* Connection stats (stress mode) */
    uint64_t        connect_attempts;
    uint64_t        connect_successes;
    uint64_t        connect_failures;
    uint64_t        disconnects;

} TestStats;

/* ========================================================================== */
/* Echo Retry Queue (for backpressure handling)                                */
/* ========================================================================== */

#define ECHO_QUEUE_SIZE     64
#define ECHO_BUFFER_SIZE    8192

typedef struct {
    uint8_t         data[ECHO_BUFFER_SIZE];
    uint16_t        len;
    PeerTalk_PeerID peer_id;
    int             valid;
} EchoSlot;

typedef struct {
    EchoSlot        slots[ECHO_QUEUE_SIZE];
    int             head;               /* Next slot to write */
    int             tail;               /* Next slot to read */
    int             count;              /* Number of pending echoes */
} EchoQueue;

/* ========================================================================== */
/* Log Reception State                                                         */
/* ========================================================================== */

/* Protocol header from Mac - must match log_stream.h */
#define LOG_STREAM_MARKER       "LOG:"
#define LOG_STREAM_MARKER_LEN   4
#define LOG_STREAM_HEADER_SIZE  8
#define LOG_RECEIVE_BUFFER_SIZE (128 * 1024)  /* 128KB for larger test logs */

typedef struct {
    uint8_t        *buffer;           /* Reception buffer */
    uint32_t        expected_length;  /* Total bytes expected */
    uint32_t        received_length;  /* Bytes received so far */
    PeerTalk_PeerID peer_id;          /* Peer sending logs */
    uint32_t        peer_ip;          /* Peer IP for folder selection */
    int             active;           /* 1 if receiving logs */
    int             overflow;         /* 1 if buffer overflowed */
    char            peer_name[64];    /* For filename */
} LogReceiver;

/* ========================================================================== */
/* Test Metrics Extraction                                                     */
/* ========================================================================== */

#define MAX_SIZE_ENTRIES 8

typedef struct {
    /* Latency metrics (per message size) */
    struct {
        int             size;
        uint32_t        min_ms;
        uint32_t        max_ms;
        uint32_t        avg_ms;
        uint32_t        sent;
        uint32_t        recv;
        uint32_t        lost;
    } latency[MAX_SIZE_ENTRIES];
    int latency_count;

    /* Throughput metrics (per buffer size) */
    struct {
        int             size;
        float           kb_per_sec;
        uint32_t        messages;
        uint32_t        duration_sec;
    } throughput[MAX_SIZE_ENTRIES];
    int throughput_count;

    /* Stress metrics */
    uint32_t        stress_cycles;
    uint32_t        stress_success;
    uint32_t        stress_failed;
    int32_t         memory_delta;
    int             stress_passed;  /* 1 if passed, 0 if failed, -1 if not present */

    /* Discovery metrics */
    uint32_t        discovery_total;
    uint32_t        discovery_unique;
    uint32_t        discovery_lost;
    float           discovery_rate;  /* per minute */

    /* Test identification */
    char            test_type[32];
} TestMetrics;

/* Echo error tracking */
typedef struct {
    uint64_t        would_block;
    uint64_t        buffer_full;
    uint64_t        connection_reset;
    uint64_t        other;
} EchoErrors;

static EchoErrors g_echo_errors = {0};

/* ========================================================================== */
/* Globals                                                                     */
/* ========================================================================== */

static PeerTalk_Context *g_ctx = NULL;
static volatile int g_running = 1;
static TestConfig g_config;
static TestStats g_stats;
static PeerTalk_PeerID g_connected_peer = 0;
static int g_streaming = 0;
static uint8_t *g_stream_buffer = NULL;
static LogReceiver g_log_receiver = {0};
static EchoQueue g_echo_queue = {0};

/* ========================================================================== */
/* Utility Functions                                                           */
/* ========================================================================== */

__attribute__((unused))
static uint64_t get_time_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

static void ip_to_str(uint32_t ip, char *buf, size_t len)
{
    snprintf(buf, len, "%u.%u.%u.%u",
             (ip >> 24) & 0xFF,
             (ip >> 16) & 0xFF,
             (ip >> 8) & 0xFF,
             ip & 0xFF);
}

static void sigint_handler(int sig)
{
    (void)sig;
    printf("\nReceived signal, shutting down...\n");
    g_running = 0;
}

/* ========================================================================== */
/* Test Metrics Parsing                                                        */
/* ========================================================================== */

/**
 * Parse test metrics from log content
 *
 * Extracts structured metrics from Mac test log data for analysis and
 * summary reporting.
 */
static void parse_test_metrics(const char *data, size_t len, TestMetrics *m)
{
    const char *p = data;
    const char *end = data + len;

    memset(m, 0, sizeof(*m));
    m->stress_passed = -1;  /* Not present by default */

    /* Detect test type from header */
    if (memmem(data, len, "Latency Test", 12)) {
        strncpy(m->test_type, "latency", sizeof(m->test_type) - 1);
    } else if (memmem(data, len, "Throughput Test", 15)) {
        strncpy(m->test_type, "throughput", sizeof(m->test_type) - 1);
    } else if (memmem(data, len, "Stress Test", 11)) {
        strncpy(m->test_type, "stress", sizeof(m->test_type) - 1);
    } else if (memmem(data, len, "Discovery Test", 14)) {
        strncpy(m->test_type, "discovery", sizeof(m->test_type) - 1);
    } else if (memmem(data, len, "MacTCP Test", 11)) {
        strncpy(m->test_type, "mactcp", sizeof(m->test_type) - 1);
    } else {
        strncpy(m->test_type, "unknown", sizeof(m->test_type) - 1);
    }

    /* Parse latency results: "SIZE 256: min=12 max=45 avg=23 ms (sent=100 recv=98 lost=2)" */
    while (p < end && m->latency_count < MAX_SIZE_ENTRIES) {
        const char *line = memmem(p, end - p, "SIZE ", 5);
        if (!line) break;

        int size, min_v, max_v, avg_v, sent, recv, lost;
        if (sscanf(line, "SIZE %d: min=%d max=%d avg=%d ms (sent=%d recv=%d lost=%d)",
                   &size, &min_v, &max_v, &avg_v, &sent, &recv, &lost) == 7) {
            int i = m->latency_count++;
            m->latency[i].size = size;
            m->latency[i].min_ms = min_v;
            m->latency[i].max_ms = max_v;
            m->latency[i].avg_ms = avg_v;
            m->latency[i].sent = sent;
            m->latency[i].recv = recv;
            m->latency[i].lost = lost;
        }
        p = line + 5;
    }

    /* Parse throughput results: "THROUGHPUT 1024: 125.3 KB/s (4521 messages in 30 sec)" */
    p = data;
    while (p < end && m->throughput_count < MAX_SIZE_ENTRIES) {
        const char *line = memmem(p, end - p, "THROUGHPUT ", 11);
        if (!line) break;

        int size, msgs, dur;
        float kbps;
        if (sscanf(line, "THROUGHPUT %d: %f KB/s (%d messages in %d sec)",
                   &size, &kbps, &msgs, &dur) >= 2) {
            int i = m->throughput_count++;
            m->throughput[i].size = size;
            m->throughput[i].kb_per_sec = kbps;
            m->throughput[i].messages = msgs;
            m->throughput[i].duration_sec = dur;
        }
        p = line + 11;
    }

    /* Parse stress results: "STRESS: 50 cycles, 48 success, 2 failed, memory delta: -2048" */
    {
        const char *line = memmem(data, len, "STRESS:", 7);
        if (line) {
            int cycles, success, failed, delta;
            if (sscanf(line, "STRESS: %d cycles, %d success, %d failed, memory delta: %d",
                       &cycles, &success, &failed, &delta) == 4) {
                m->stress_cycles = cycles;
                m->stress_success = success;
                m->stress_failed = failed;
                m->memory_delta = delta;
            }
        }
        /* Check for pass/fail */
        if (memmem(data, len, "STRESS TEST: PASSED", 19)) {
            m->stress_passed = 1;
        } else if (memmem(data, len, "STRESS TEST: FAILED", 19)) {
            m->stress_passed = 0;
        }
    }

    /* Parse discovery results: "DISCOVERY: 156 total, 4 unique peers, 2 lost events" */
    {
        const char *line = memmem(data, len, "DISCOVERY:", 10);
        if (line) {
            int total, unique, lost_v;
            if (sscanf(line, "DISCOVERY: %d total, %d unique peers, %d lost",
                       &total, &unique, &lost_v) == 3) {
                m->discovery_total = total;
                m->discovery_unique = unique;
                m->discovery_lost = lost_v;
            }
            /* Also check for rate */
            const char *rate_line = memmem(data, len, "discoveries/min", 15);
            if (rate_line) {
                /* Scan backwards to find the number */
                const char *num_start = rate_line - 1;
                while (num_start > data && (*num_start == '.' || (*num_start >= '0' && *num_start <= '9'))) {
                    num_start--;
                }
                float rate;
                if (sscanf(num_start, "%f", &rate) == 1) {
                    m->discovery_rate = rate;
                }
            }
        }
    }
}

/**
 * Print metrics summary to stdout
 */
static void print_metrics_summary(const TestMetrics *m)
{
    int i;

    printf("\n");
    printf("========== %s METRICS ==========\n", m->test_type);

    if (m->latency_count > 0) {
        printf("\nLatency Results:\n");
        printf("  Size    Min    Avg    Max    Loss\n");
        printf("  ----    ---    ---    ---    ----\n");
        for (i = 0; i < m->latency_count; i++) {
            int loss_pct = (m->latency[i].sent > 0) ?
                          (m->latency[i].lost * 100 / m->latency[i].sent) : 0;
            printf("  %4dB   %3ums  %3ums  %3ums  %d%%\n",
                   m->latency[i].size,
                   m->latency[i].min_ms,
                   m->latency[i].avg_ms,
                   m->latency[i].max_ms,
                   loss_pct);
        }
    }

    if (m->throughput_count > 0) {
        printf("\nThroughput Results:\n");
        printf("  Size    KB/s    Messages\n");
        printf("  ----    ----    --------\n");
        for (i = 0; i < m->throughput_count; i++) {
            printf("  %4dB   %5.1f   %u\n",
                   m->throughput[i].size,
                   m->throughput[i].kb_per_sec,
                   m->throughput[i].messages);
        }
    }

    if (m->stress_cycles > 0) {
        printf("\nStress Results:\n");
        printf("  Cycles: %u (%u success, %u failed)\n",
               m->stress_cycles, m->stress_success, m->stress_failed);
        printf("  Memory delta: %d bytes\n", m->memory_delta);
        if (m->stress_passed >= 0) {
            printf("  Status: %s\n", m->stress_passed ? "PASSED" : "FAILED");
        }
    }

    if (m->discovery_total > 0) {
        printf("\nDiscovery Results:\n");
        printf("  Total discoveries: %u\n", m->discovery_total);
        printf("  Unique peers: %u\n", m->discovery_unique);
        printf("  Lost events: %u\n", m->discovery_lost);
        if (m->discovery_rate > 0) {
            printf("  Rate: %.1f discoveries/min\n", m->discovery_rate);
        }
    }

    printf("=====================================\n\n");
}

/* ========================================================================== */
/* Echo Mode (Latency Testing)                                                 */
/* ========================================================================== */

/**
 * Latency test protocol:
 * - Mac sends: [8-byte timestamp_us][payload]
 * - POSIX echoes: [same 8-byte timestamp_us][same payload]
 * - Mac calculates RTT from timestamp
 */

/**
 * Queue an echo for retry when send fails due to backpressure
 */
static int echo_queue_push(PeerTalk_PeerID peer_id, const void *data, uint16_t len)
{
    if (g_echo_queue.count >= ECHO_QUEUE_SIZE) {
        return -1;  /* Queue full */
    }
    if (len > ECHO_BUFFER_SIZE) {
        return -1;  /* Message too large */
    }

    EchoSlot *slot = &g_echo_queue.slots[g_echo_queue.head];
    memcpy(slot->data, data, len);
    slot->len = len;
    slot->peer_id = peer_id;
    slot->valid = 1;

    g_echo_queue.head = (g_echo_queue.head + 1) % ECHO_QUEUE_SIZE;
    g_echo_queue.count++;
    return 0;
}

/**
 * Drain pending echoes from retry queue
 * Called from main loop to handle backpressure recovery
 */
static void echo_queue_drain(PeerTalk_Context *ctx)
{
    int drain_count = 0;
    const int max_drain = 16;  /* Match Mac's burst size */

    while (g_echo_queue.count > 0 && drain_count < max_drain) {
        EchoSlot *slot = &g_echo_queue.slots[g_echo_queue.tail];
        if (!slot->valid) break;

        PeerTalk_Error err = PeerTalk_Send(ctx, slot->peer_id, slot->data, slot->len);
        if (err == PT_OK) {
            g_stats.echo_count++;
            g_stats.echo_bytes += slot->len;
            g_stats.echo_retries++;
            slot->valid = 0;
            g_echo_queue.tail = (g_echo_queue.tail + 1) % ECHO_QUEUE_SIZE;
            g_echo_queue.count--;
            drain_count++;
        } else if (err == PT_ERR_WOULD_BLOCK || err == PT_ERR_BUFFER_FULL) {
            /* Still backpressured, try again next poll */
            break;
        } else {
            /* Real error, drop this echo */
            g_stats.echo_drops++;
            slot->valid = 0;
            g_echo_queue.tail = (g_echo_queue.tail + 1) % ECHO_QUEUE_SIZE;
            g_echo_queue.count--;
        }
    }
}

static void echo_message(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                         const void *data, uint16_t len)
{
    /* Try to echo immediately */
    PeerTalk_Error err = PeerTalk_Send(ctx, peer_id, data, len);
    if (err == PT_OK) {
        g_stats.echo_count++;
        g_stats.echo_bytes += len;

        if (g_config.verbose && (g_stats.echo_count % 100 == 0)) {
            printf("[ECHO] %llu messages echoed\n",
                   (unsigned long long)g_stats.echo_count);
        }
    } else if (err == PT_ERR_WOULD_BLOCK) {
        /* Backpressure - queue for retry instead of dropping */
        g_echo_errors.would_block++;
        if (echo_queue_push(peer_id, data, len) < 0) {
            g_stats.echo_drops++;
            if (g_config.verbose) {
                printf("[ECHO] Queue full, dropped echo len=%u\n", len);
            }
        }
    } else if (err == PT_ERR_BUFFER_FULL) {
        /* Buffer full - queue for retry */
        g_echo_errors.buffer_full++;
        if (echo_queue_push(peer_id, data, len) < 0) {
            g_stats.echo_drops++;
            if (g_config.verbose) {
                printf("[ECHO] Queue full, dropped echo len=%u\n", len);
            }
        }
    } else {
        /* Real error - track by type */
        if (err == PT_ERR_NOT_CONNECTED || err == PT_ERR_CONNECTION_CLOSED) {
            g_echo_errors.connection_reset++;
        } else {
            g_echo_errors.other++;
        }
        static int fail_logged = 0;
        if (fail_logged < 10) {
            printf("[ECHO] Send failed: err=%d len=%u peer=%u\n", err, len, peer_id);
            fail_logged++;
        }
    }
}

/* ========================================================================== */
/* Stream Mode (Throughput Testing)                                            */
/* ========================================================================== */

/**
 * Throughput test protocol:
 * - POSIX streams: [4-byte sequence][payload]
 * - Mac counts received bytes
 * - On completion, Mac sends ACK with stats
 */

static void start_streaming(void)
{
    if (g_connected_peer == 0) {
        printf("[STREAM] No peer connected, cannot start streaming\n");
        return;
    }

    /* Allocate stream buffer if needed */
    if (g_stream_buffer == NULL) {
        g_stream_buffer = malloc(g_config.message_size);
        if (g_stream_buffer == NULL) {
            printf("[STREAM] Failed to allocate buffer\n");
            return;
        }
        /* Fill with pattern for debugging */
        for (int i = 0; i < g_config.message_size; i++) {
            g_stream_buffer[i] = (uint8_t)(i & 0xFF);
        }
    }

    gettimeofday(&g_stats.stream_start, NULL);
    g_streaming = 1;
    g_stats.bytes_sent = 0;
    g_stats.messages_sent = 0;

    printf("[STREAM] Starting stream: %d messages of %d bytes each\n",
           g_config.message_count, g_config.message_size);
}

static void stream_tick(void)
{
    int burst_count = 0;
    const int max_burst = 10;  /* Match Mac's burst size */

    if (!g_streaming || g_connected_peer == 0)
        return;

    if (g_stats.messages_sent >= (uint64_t)g_config.message_count) {
        /* Done streaming */
        gettimeofday(&g_stats.stream_end, NULL);
        g_streaming = 0;

        double elapsed = (g_stats.stream_end.tv_sec - g_stats.stream_start.tv_sec) +
                        (g_stats.stream_end.tv_usec - g_stats.stream_start.tv_usec) / 1000000.0;
        double throughput = (elapsed > 0) ? (g_stats.bytes_sent / elapsed) : 0;

        printf("[STREAM] Complete: %llu bytes in %.2f sec = %.2f KB/s\n",
               (unsigned long long)g_stats.bytes_sent, elapsed, throughput / 1024.0);
        return;
    }

    /* Send messages in bursts (like Mac test apps do) for maximum throughput */
    while (burst_count < max_burst &&
           g_stats.messages_sent < (uint64_t)g_config.message_count) {
        /* Add sequence number to first 4 bytes */
        uint32_t seq = (uint32_t)g_stats.messages_sent;
        memcpy(g_stream_buffer, &seq, sizeof(seq));

        PeerTalk_Error err = PeerTalk_Send(g_ctx, g_connected_peer, g_stream_buffer,
                                            g_config.message_size);
        if (err == PT_OK) {
            g_stats.bytes_sent += g_config.message_size;
            g_stats.messages_sent++;
            burst_count++;
        } else if (err == PT_ERR_WOULD_BLOCK || err == PT_ERR_BUFFER_FULL) {
            /* Backpressure - stop burst and let poll drain */
            break;
        } else {
            /* Real error */
            break;
        }
    }
}

/* ========================================================================== */
/* Log Reception                                                               */
/* ========================================================================== */

/**
 * Initialize log receiver for a peer
 */
static int log_receiver_start(PeerTalk_PeerID peer_id, uint32_t peer_ip,
                               const void *header_data, uint16_t header_len,
                               const char *peer_name)
{
    uint32_t total_length;

    if (header_len < LOG_STREAM_HEADER_SIZE) {
        return -1;
    }

    /* Extract total length from header (big-endian from Mac) */
    memcpy(&total_length, (const uint8_t *)header_data + LOG_STREAM_MARKER_LEN,
           sizeof(total_length));
    total_length = ntohl(total_length);  /* Convert from network to host order */

    if (total_length > LOG_RECEIVE_BUFFER_SIZE || total_length < LOG_STREAM_HEADER_SIZE) {
        printf("[LOG] Invalid log stream length: %u\n", total_length);
        return -1;
    }

    /* Allocate buffer if needed */
    if (!g_log_receiver.buffer) {
        g_log_receiver.buffer = malloc(LOG_RECEIVE_BUFFER_SIZE);
        if (!g_log_receiver.buffer) {
            printf("[LOG] Failed to allocate receive buffer\n");
            return -1;
        }
    }

    g_log_receiver.expected_length = total_length;
    g_log_receiver.received_length = 0;
    g_log_receiver.peer_id = peer_id;
    g_log_receiver.peer_ip = peer_ip;
    g_log_receiver.active = 1;
    strncpy(g_log_receiver.peer_name, peer_name ? peer_name : "unknown", 63);
    g_log_receiver.peer_name[63] = '\0';

    /* Store the first chunk (includes header) */
    memcpy(g_log_receiver.buffer, header_data, header_len);
    g_log_receiver.received_length = header_len;

    printf("[LOG] Started receiving logs from %s (IP 0x%08X, %u bytes expected)\n",
           g_log_receiver.peer_name, peer_ip, total_length);

    return 0;
}

/**
 * Append data to log receiver buffer
 */
static void log_receiver_append(const void *data, uint16_t len)
{
    uint32_t remaining;

    if (!g_log_receiver.active) return;

    remaining = g_log_receiver.expected_length - g_log_receiver.received_length;
    if (len > remaining) len = remaining;

    memcpy(g_log_receiver.buffer + g_log_receiver.received_length, data, len);
    g_log_receiver.received_length += len;

    if (g_config.verbose) {
        printf("[LOG] Received %u/%u bytes\n",
               g_log_receiver.received_length, g_log_receiver.expected_length);
    }
}

/* ========================================================================== */
/* Machine Registry (configurable via environment)                             */
/* ========================================================================== */

#define MAX_MACHINE_ENTRIES 16

typedef struct {
    uint32_t    ip;
    char        name[32];
} MachineEntry;

static MachineEntry g_machine_registry[MAX_MACHINE_ENTRIES];
static int g_machine_count = 0;
static int g_registry_loaded = 0;

/**
 * Parse IP address string to uint32_t
 */
static uint32_t parse_ip(const char *ip_str)
{
    unsigned int a, b, c, d;
    if (sscanf(ip_str, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
        return ((a & 0xFF) << 24) | ((b & 0xFF) << 16) |
               ((c & 0xFF) << 8) | (d & 0xFF);
    }
    return 0;
}

/**
 * Load machine registry from MACHINE_REGISTRY environment variable
 *
 * Format: "IP:name,IP:name,..."
 * Example: "10.188.1.55:macse,10.188.1.213:performa6200"
 */
static void load_machine_registry(void)
{
    const char *env;
    char *copy, *token, *saveptr;

    if (g_registry_loaded) return;
    g_registry_loaded = 1;

    /* Default entries (fallback if env not set) */
    g_machine_registry[0].ip = 0x0ABC0137;  /* 10.188.1.55 */
    strncpy(g_machine_registry[0].name, "macse", sizeof(g_machine_registry[0].name) - 1);
    g_machine_registry[1].ip = 0x0ABC01D5;  /* 10.188.1.213 */
    strncpy(g_machine_registry[1].name, "performa6200", sizeof(g_machine_registry[1].name) - 1);
    g_machine_count = 2;

    /* Override with environment variable if set */
    env = getenv("MACHINE_REGISTRY");
    if (!env || !*env) return;

    copy = strdup(env);
    if (!copy) return;

    g_machine_count = 0;
    token = strtok_r(copy, ",", &saveptr);
    while (token && g_machine_count < MAX_MACHINE_ENTRIES) {
        char *colon = strchr(token, ':');
        if (colon) {
            *colon = '\0';
            g_machine_registry[g_machine_count].ip = parse_ip(token);
            strncpy(g_machine_registry[g_machine_count].name, colon + 1,
                    sizeof(g_machine_registry[g_machine_count].name) - 1);
            g_machine_registry[g_machine_count].name[31] = '\0';
            g_machine_count++;
        }
        token = strtok_r(NULL, ",", &saveptr);
    }

    free(copy);

    if (g_config.verbose) {
        printf("[REGISTRY] Loaded %d machines from MACHINE_REGISTRY\n", g_machine_count);
    }
}

/**
 * Determine machine folder from peer IP address
 */
static const char *get_machine_folder(uint32_t ip)
{
    int i;
    load_machine_registry();

    for (i = 0; i < g_machine_count; i++) {
        if (g_machine_registry[i].ip == ip) {
            return g_machine_registry[i].name;
        }
    }
    return "unknown";
}

/* ========================================================================== */
/* Test Name Extraction                                                        */
/* ========================================================================== */

/**
 * Extract test name from log content
 *
 * Searches for test headers like "PeerTalk Latency Test" and returns
 * lowercase test name (e.g., "latency").
 *
 * @param buffer  Log data buffer
 * @param len     Length of log data
 * @return        Static string with test name, or "test" if not found
 */
static const char *extract_test_name(const uint8_t *buffer, uint32_t len)
{
    static char name[32];
    static const struct {
        const char *pattern;
        const char *name;
    } tests[] = {
        { "Latency Test", "latency" },
        { "Throughput Test", "throughput" },
        { "Stress Test", "stress" },
        { "Discovery Test", "discovery" },
        { "MacTCP Test", "mactcp" },
        { NULL, NULL }
    };
    int i;

    for (i = 0; tests[i].pattern != NULL; i++) {
        if (memmem(buffer, len, tests[i].pattern, strlen(tests[i].pattern))) {
            strncpy(name, tests[i].name, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            return name;
        }
    }
    return "test";
}

/**
 * Check if log reception is complete and save to file
 */
static void log_receiver_check_complete(void)
{
    FILE *fp;
    char filename[256];
    char machine_folder[64];
    time_t now;
    struct tm *tm_info;
    const char *machine;
    const char *test_name;
    uint32_t log_data_offset;
    uint32_t log_data_len;

    if (!g_log_receiver.active) return;
    if (g_log_receiver.received_length < g_log_receiver.expected_length) return;

    /* Determine machine folder from peer IP */
    machine = get_machine_folder(g_log_receiver.peer_ip);
    snprintf(machine_folder, sizeof(machine_folder), "plan/performance/mactcp/%s", machine);

    /* Extract test name from log content */
    log_data_offset = LOG_STREAM_HEADER_SIZE;
    log_data_len = g_log_receiver.received_length - log_data_offset;
    test_name = extract_test_name(g_log_receiver.buffer + log_data_offset, log_data_len);

    /* Generate filename with timestamp - save to machine-specific folder */
    now = time(NULL);
    tm_info = localtime(&now);
    snprintf(filename, sizeof(filename),
             "%s/%s_%04d%02d%02d_%02d%02d%02d.log",
             machine_folder,
             test_name,
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);

    /* Create directory structure if needed */
    mkdir("plan", 0755);
    mkdir("plan/performance", 0755);
    mkdir("plan/performance/mactcp", 0755);
    mkdir(machine_folder, 0755);

    /* Write log data (skip header) */
    fp = fopen(filename, "w");
    if (fp) {
        uint32_t log_len = g_log_receiver.received_length - LOG_STREAM_HEADER_SIZE;
        fwrite(g_log_receiver.buffer + LOG_STREAM_HEADER_SIZE, 1, log_len, fp);
        fclose(fp);
        printf("[LOG] Saved %u bytes to %s\n", log_len, filename);

        /* Parse and display metrics summary */
        {
            TestMetrics metrics;
            const char *log_content = (const char *)(g_log_receiver.buffer + LOG_STREAM_HEADER_SIZE);
            parse_test_metrics(log_content, log_data_len, &metrics);
            print_metrics_summary(&metrics);
        }
    } else {
        printf("[LOG] Failed to create %s: %s\n", filename, strerror(errno));
    }

    g_log_receiver.active = 0;
}

/**
 * Handle potential log stream message
 * Returns 1 if message was handled as log data, 0 otherwise
 */
static int log_receiver_handle(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                                const void *data, uint16_t len)
{
    /* Check for LOG: marker at start of message */
    if (len >= LOG_STREAM_HEADER_SIZE &&
        memcmp(data, LOG_STREAM_MARKER, LOG_STREAM_MARKER_LEN) == 0) {

        const char *name = "unknown";
        uint32_t peer_ip = 0;
        const PeerTalk_PeerInfo *info = PeerTalk_GetPeerByID(ctx, peer_id);
        if (info) {
            name = PeerTalk_GetPeerName(ctx, info->name_idx);
            if (!name) name = "unknown";
            peer_ip = info->address;
        }

        if (g_config.verbose) {
            printf("[LOG] Detected log stream marker from peer %u (IP 0x%08X)\n",
                   peer_id, peer_ip);
        }
        log_receiver_start(peer_id, peer_ip, data, len, name);
        log_receiver_check_complete();
        return 1;
    }

    /* Check if we're receiving continuation data */
    if (g_log_receiver.active && g_log_receiver.peer_id == peer_id) {
        log_receiver_append(data, len);
        log_receiver_check_complete();
        return 1;
    }

    return 0;
}

/* ========================================================================== */
/* Callbacks                                                                   */
/* ========================================================================== */

static void on_peer_discovered(PeerTalk_Context *ctx, const PeerTalk_PeerInfo *peer,
                                void *user_data)
{
    char ip_str[32];
    const char *name;
    (void)user_data;

    ip_to_str(peer->address, ip_str, sizeof(ip_str));
    name = PeerTalk_GetPeerName(ctx, peer->name_idx);
    if (!name) name = "(unknown)";

    printf("[DISCOVERED] Peer %u: \"%s\" at %s:%u\n",
           peer->id, name, ip_str, peer->port);

    g_stats.discovery_packets_seen++;
    g_stats.unique_peers_found++;

    /* Auto-connect if requested */
    if (g_config.connect_ip && strcmp(ip_str, g_config.connect_ip) == 0) {
        printf("[ACTION] Connecting to %s...\n", ip_str);
        g_stats.connect_attempts++;
        if (PeerTalk_Connect(ctx, peer->id) == PT_OK) {
            printf("[ACTION] Connection initiated\n");
        } else {
            printf("[ERROR] Failed to initiate connection\n");
            g_stats.connect_failures++;
        }
    }
}

static void on_peer_connected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                               void *user_data)
{
    PeerTalk_Capabilities caps;
    uint16_t effective_max;
    (void)user_data;

    printf("[CONNECTED] Peer %u\n", peer_id);
    g_connected_peer = peer_id;
    g_stats.connect_successes++;

    /* Log peer capabilities */
    if (PeerTalk_GetPeerCapabilities(ctx, peer_id, &caps) == PT_OK) {
        printf("[CAPS] Peer %u: max_msg=%u chunk=%u pressure=%u%s\n",
               peer_id,
               caps.max_message_size,
               caps.preferred_chunk,
               caps.buffer_pressure,
               caps.fragmentation_active ? " [FRAG]" : "");
    }
    effective_max = PeerTalk_GetPeerMaxMessage(ctx, peer_id);
    printf("[CAPS] Effective max message: %u bytes\n", effective_max);

    /* Start streaming if in stream mode */
    if (g_config.mode == MODE_STREAM) {
        start_streaming();
    }
}

static void on_peer_disconnected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                                  int reason, void *user_data)
{
    (void)ctx;
    (void)user_data;

    printf("[DISCONNECTED] Peer %u (reason=%d)\n", peer_id, reason);
    if (g_connected_peer == peer_id) {
        g_connected_peer = 0;
        g_streaming = 0;
    }
    g_stats.disconnects++;
}

static void on_message_received(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                                 const void *data, uint16_t len, void *user_data)
{
    (void)user_data;

    g_stats.messages_received++;
    g_stats.bytes_received += len;

    if (g_config.verbose) {
        printf("[MESSAGE] From peer %u: %u bytes\n", peer_id, len);
    }

    /* Check for log stream data (all modes) */
    if (log_receiver_handle(ctx, peer_id, data, len)) {
        return;  /* Message was log data, don't process further */
    }

    switch (g_config.mode) {
    case MODE_ECHO:
        echo_message(ctx, peer_id, data, len);
        break;

    case MODE_STREAM:
        /* In stream mode, received messages might be ACKs or commands */
        if (len >= 4) {
            const char *cmd = (const char *)data;
            if (strncmp(cmd, "ACK", 3) == 0) {
                printf("[STREAM] Received ACK from peer\n");
            } else if (strncmp(cmd, "START", 5) == 0) {
                start_streaming();
            } else if (strncmp(cmd, "STOP", 4) == 0) {
                g_streaming = 0;
                printf("[STREAM] Stopped by peer request\n");
            }
        }
        break;

    case MODE_STRESS:
        /* In stress mode, echo back to confirm connection */
        PeerTalk_Send(ctx, peer_id, "ACK", 3);
        break;

    case MODE_DISCOVERY:
        /* Just count, don't respond */
        break;
    }
}

/* ========================================================================== */
/* Main                                                                        */
/* ========================================================================== */

static void print_usage(const char *prog)
{
    printf("PeerTalk Performance Test Partner\n");
    printf("\n");
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  --mode MODE       Test mode: echo (default), stream, stress, discovery\n");
    printf("  --port PORT       Discovery port (default: 7353)\n");
    printf("  --connect IP      Auto-connect to specified IP\n");
    printf("  --size BYTES      Message size for streaming (default: 1024)\n");
    printf("  --count N         Number of messages (default: 1000)\n");
    printf("  --duration SEC    Test duration in seconds (default: 0 = forever)\n");
    printf("  --verbose         Enable verbose logging\n");
    printf("  --fast            No sleep in main loop (high CPU, minimal latency)\n");
    printf("  --help            Show this help\n");
    printf("\n");
    printf("Modes:\n");
    printf("  echo      Echo all received messages (for latency testing)\n");
    printf("  stream    Stream data to connected peer (for throughput testing)\n");
    printf("  stress    Rapid connect/disconnect cycles\n");
    printf("  discovery Count discovery packets only\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s                            # Run as echo server\n", prog);
    printf("  %s --mode stream --size 4096  # Stream 4KB messages\n", prog);
    printf("  %s --connect 192.168.1.50     # Auto-connect to Mac\n", prog);
}

static void print_stats(void)
{
    printf("\n");
    printf("========================================\n");
    printf("PERFORMANCE TEST RESULTS\n");
    printf("========================================\n");

    printf("\nDiscovery:\n");
    printf("  Packets seen: %llu\n", (unsigned long long)g_stats.discovery_packets_seen);
    printf("  Unique peers: %llu\n", (unsigned long long)g_stats.unique_peers_found);

    printf("\nConnections:\n");
    printf("  Attempts: %llu\n", (unsigned long long)g_stats.connect_attempts);
    printf("  Successes: %llu\n", (unsigned long long)g_stats.connect_successes);
    printf("  Failures: %llu\n", (unsigned long long)g_stats.connect_failures);
    printf("  Disconnects: %llu\n", (unsigned long long)g_stats.disconnects);

    printf("\nMessages:\n");
    printf("  Received: %llu (%llu bytes)\n",
           (unsigned long long)g_stats.messages_received,
           (unsigned long long)g_stats.bytes_received);
    printf("  Sent/Echoed: %llu (%llu bytes)\n",
           (unsigned long long)g_stats.echo_count,
           (unsigned long long)g_stats.echo_bytes);
    if (g_stats.echo_retries > 0 || g_stats.echo_drops > 0) {
        printf("  Echo retries: %llu, drops: %llu\n",
               (unsigned long long)g_stats.echo_retries,
               (unsigned long long)g_stats.echo_drops);
    }

    /* Show detailed echo errors if any occurred */
    if (g_echo_errors.would_block > 0 || g_echo_errors.buffer_full > 0 ||
        g_echo_errors.connection_reset > 0 || g_echo_errors.other > 0) {
        printf("\nEcho Errors (by type):\n");
        if (g_echo_errors.would_block > 0)
            printf("  would_block: %llu\n", (unsigned long long)g_echo_errors.would_block);
        if (g_echo_errors.buffer_full > 0)
            printf("  buffer_full: %llu\n", (unsigned long long)g_echo_errors.buffer_full);
        if (g_echo_errors.connection_reset > 0)
            printf("  connection_reset: %llu\n", (unsigned long long)g_echo_errors.connection_reset);
        if (g_echo_errors.other > 0)
            printf("  other: %llu\n", (unsigned long long)g_echo_errors.other);
    }

    if (g_config.mode == MODE_STREAM && g_stats.bytes_sent > 0) {
        double elapsed = (g_stats.stream_end.tv_sec - g_stats.stream_start.tv_sec) +
                        (g_stats.stream_end.tv_usec - g_stats.stream_start.tv_usec) / 1000000.0;
        if (elapsed > 0) {
            printf("\nThroughput:\n");
            printf("  Bytes sent: %llu\n", (unsigned long long)g_stats.bytes_sent);
            printf("  Duration: %.2f sec\n", elapsed);
            printf("  Rate: %.2f KB/s\n", (g_stats.bytes_sent / elapsed) / 1024.0);
        }
    }

    printf("========================================\n");
}

int main(int argc, char *argv[])
{
    PeerTalk_Config config;
    PeerTalk_Callbacks callbacks;
    time_t start_time, now;
    time_t last_status = 0;

    /* Default configuration */
    memset(&g_config, 0, sizeof(g_config));
    g_config.mode = MODE_ECHO;
    g_config.discovery_port = 7353;
    g_config.tcp_port = 7354;
    g_config.message_size = 1024;
    g_config.message_count = 1000;
    g_config.duration_sec = 0;  /* 0 = run forever until Ctrl+C */

    memset(&g_stats, 0, sizeof(g_stats));

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            const char *mode = argv[++i];
            if (strcmp(mode, "echo") == 0) g_config.mode = MODE_ECHO;
            else if (strcmp(mode, "stream") == 0) g_config.mode = MODE_STREAM;
            else if (strcmp(mode, "stress") == 0) g_config.mode = MODE_STRESS;
            else if (strcmp(mode, "discovery") == 0) g_config.mode = MODE_DISCOVERY;
            else {
                fprintf(stderr, "Unknown mode: %s\n", mode);
                return 1;
            }
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            g_config.discovery_port = atoi(argv[++i]);
            g_config.tcp_port = g_config.discovery_port + 1;
        } else if (strcmp(argv[i], "--connect") == 0 && i + 1 < argc) {
            g_config.connect_ip = argv[++i];
        } else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            g_config.message_size = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            g_config.message_count = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            g_config.duration_sec = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--verbose") == 0) {
            g_config.verbose = 1;
        } else if (strcmp(argv[i], "--fast") == 0) {
            g_config.fast = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    /* Signal handlers for graceful shutdown */
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    printf("========================================\n");
    printf("PeerTalk Performance Test Partner\n");
    printf("Version: %s\n", PeerTalk_Version());
    printf("Mode: %s%s\n",
           g_config.mode == MODE_ECHO ? "echo" :
           g_config.mode == MODE_STREAM ? "stream" :
           g_config.mode == MODE_STRESS ? "stress" : "discovery",
           g_config.fast ? " (FAST)" : "");
    printf("========================================\n\n");

    /* Initialize PeerTalk */
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "PerfPartner", PT_MAX_PEER_NAME);
    config.max_peers = 16;
    config.discovery_port = g_config.discovery_port;
    config.tcp_port = g_config.tcp_port;

    printf("Initializing PeerTalk...\n");
    g_ctx = PeerTalk_Init(&config);
    if (!g_ctx) {
        fprintf(stderr, "FAILED to initialize PeerTalk!\n");
        return 1;
    }

    /* Set callbacks */
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.on_peer_discovered = on_peer_discovered;
    callbacks.on_peer_connected = on_peer_connected;
    callbacks.on_peer_disconnected = on_peer_disconnected;
    callbacks.on_message_received = on_message_received;
    PeerTalk_SetCallbacks(g_ctx, &callbacks);

    /* Start discovery */
    printf("Starting discovery on port %d...\n", g_config.discovery_port);
    if (PeerTalk_StartDiscovery(g_ctx) != PT_OK) {
        fprintf(stderr, "FAILED to start discovery!\n");
        PeerTalk_Shutdown(g_ctx);
        return 1;
    }

    /* Start listening */
    printf("Starting TCP listener on port %d...\n", g_config.tcp_port);
    if (PeerTalk_StartListening(g_ctx) != PT_OK) {
        fprintf(stderr, "FAILED to start listening!\n");
        PeerTalk_Shutdown(g_ctx);
        return 1;
    }

    if (g_config.duration_sec > 0) {
        printf("Running for %d seconds... (Ctrl+C to stop early)\n\n", g_config.duration_sec);
    } else {
        printf("Running until Ctrl+C...\n\n");
    }

    start_time = time(NULL);
    last_status = start_time;

    /* Main loop
     *
     * SDK Usage Note: We use PeerTalk_Poll() for normal operation, but switch
     * to PeerTalk_PollFast() in echo mode with --fast flag for minimum latency.
     * PollFast skips discovery and periodic work, only doing TCP I/O.
     */
    while (g_running) {
        /* Choose poll function based on mode and fast flag
         * SDK Pattern: PollFast for tight loops, regular Poll for full functionality
         */
        if (g_config.fast && g_config.mode == MODE_ECHO && g_connected_peer != 0) {
            /* Fast path: PollFast for minimal latency echo during active connection */
            PeerTalk_PollFast(g_ctx);
        } else {
            /* Normal path: full Poll for discovery, connection management, etc. */
            PeerTalk_Poll(g_ctx);
        }

        /* Drain echo retry queue (handles backpressure recovery)
         * SDK Pattern: Handle WOULD_BLOCK by queuing and retrying, not dropping
         */
        if (g_config.mode == MODE_ECHO) {
            echo_queue_drain(g_ctx);
        }

        /* Handle streaming if active
         * SDK Pattern: Send in bursts to maximize throughput, handle backpressure
         */
        if (g_streaming) {
            stream_tick();
        }

        /* Status update every 10 seconds */
        now = time(NULL);
        if (now - last_status >= 10) {
            printf("[STATUS] %ld sec: peers=%llu, msgs=%llu, connected=%s",
                   (long)(now - start_time),
                   (unsigned long long)g_stats.unique_peers_found,
                   (unsigned long long)g_stats.messages_received,
                   g_connected_peer ? "yes" : "no");
            if (g_echo_queue.count > 0) {
                printf(", echo_queue=%d", g_echo_queue.count);
            }
            printf("\n");
            last_status = now;
        }

        /* Check duration (0 = run forever) */
        if (g_config.duration_sec > 0 && (now - start_time >= g_config.duration_sec)) {
            printf("Test duration reached.\n");
            break;
        }

        /* Sleep to avoid busy-wait, unless --fast mode for minimal latency.
         * In fast mode, we spin at 100% CPU for lowest possible echo latency.
         * SDK Note: Fast mode + PollFast = maximum performance for echo server. */
        if (!g_config.fast) {
            usleep(1000);  /* 1ms sleep to avoid busy-wait */
        }
    }

    print_stats();

    /* Cleanup */
    printf("========================================\n");
    printf("Shutting down gracefully...\n");
    printf("========================================\n");

    if (g_stream_buffer) {
        free(g_stream_buffer);
    }
    if (g_log_receiver.buffer) {
        free(g_log_receiver.buffer);
    }
    PeerTalk_Shutdown(g_ctx);

    printf("Partner exited cleanly.\n");
    return 0;
}
