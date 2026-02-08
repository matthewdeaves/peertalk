/* Feature test macros - must be before any includes */
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
} TestConfig;

typedef struct {
    /* Latency stats (echo mode) */
    uint64_t        echo_count;
    uint64_t        echo_bytes;
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
/* Globals                                                                     */
/* ========================================================================== */

static PeerTalk_Context *g_ctx = NULL;
static volatile int g_running = 1;
static TestConfig g_config;
static TestStats g_stats;
static PeerTalk_PeerID g_connected_peer = 0;
static int g_streaming = 0;
static uint8_t *g_stream_buffer = NULL;

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
/* Echo Mode (Latency Testing)                                                 */
/* ========================================================================== */

/**
 * Latency test protocol:
 * - Mac sends: [8-byte timestamp_us][payload]
 * - POSIX echoes: [same 8-byte timestamp_us][same payload]
 * - Mac calculates RTT from timestamp
 */

static void echo_message(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                         const void *data, uint16_t len)
{
    /* Echo the message back exactly as received */
    if (PeerTalk_Send(ctx, peer_id, data, len) == PT_OK) {
        g_stats.echo_count++;
        g_stats.echo_bytes += len;

        if (g_config.verbose && (g_stats.echo_count % 100 == 0)) {
            printf("[ECHO] %llu messages echoed\n",
                   (unsigned long long)g_stats.echo_count);
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

    /* Send next message with sequence number */
    uint32_t seq = (uint32_t)g_stats.messages_sent;
    memcpy(g_stream_buffer, &seq, sizeof(seq));

    if (PeerTalk_Send(g_ctx, g_connected_peer, g_stream_buffer,
                      g_config.message_size) == PT_OK) {
        g_stats.bytes_sent += g_config.message_size;
        g_stats.messages_sent++;
    }
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
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    /* Signal handler */
    signal(SIGINT, sigint_handler);

    printf("========================================\n");
    printf("PeerTalk Performance Test Partner\n");
    printf("Version: %s\n", PeerTalk_Version());
    printf("Mode: %s\n",
           g_config.mode == MODE_ECHO ? "echo" :
           g_config.mode == MODE_STREAM ? "stream" :
           g_config.mode == MODE_STRESS ? "stress" : "discovery");
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

    /* Main loop */
    while (g_running) {
        PeerTalk_Poll(g_ctx);

        /* Handle streaming if active */
        if (g_streaming) {
            stream_tick();
        }

        /* Status update every 10 seconds */
        now = time(NULL);
        if (now - last_status >= 10) {
            printf("[STATUS] %ld sec: peers=%llu, msgs=%llu, connected=%s\n",
                   (long)(now - start_time),
                   (unsigned long long)g_stats.unique_peers_found,
                   (unsigned long long)g_stats.messages_received,
                   g_connected_peer ? "yes" : "no");
            last_status = now;
        }

        /* Check duration (0 = run forever) */
        if (g_config.duration_sec > 0 && (now - start_time >= g_config.duration_sec)) {
            printf("Test duration reached.\n");
            break;
        }

        usleep(1000);  /* 1ms sleep to avoid busy-wait */
    }

    print_stats();

    /* Cleanup */
    if (g_stream_buffer) {
        free(g_stream_buffer);
    }
    PeerTalk_Shutdown(g_ctx);

    return 0;
}
