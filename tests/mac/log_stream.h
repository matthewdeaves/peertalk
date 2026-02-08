/**
 * @file log_stream.h
 * @brief Log Streaming Helper for Mac Test Apps
 *
 * Provides automatic log capture and streaming to the test partner.
 * Uses PT_Log callback to buffer logs in memory, then streams the
 * buffer to the connected peer at test completion.
 *
 * Usage:
 *   1. Call log_stream_init() after creating PT_Log
 *   2. Run your test normally (logs are captured automatically)
 *   3. Call log_stream_send() when test completes
 *   4. Poll until log_stream_complete() returns true
 *   5. Call log_stream_cleanup() before shutdown
 *
 * Memory: Uses a fixed 32KB buffer. Logs exceeding this are truncated.
 */

#ifndef LOG_STREAM_H
#define LOG_STREAM_H

#include "peertalk.h"
#include "pt_log.h"

/* Log buffer size - 32KB should hold most test logs */
#define LOG_STREAM_BUFFER_SIZE  32768

/* Protocol header - partner looks for this to identify log streams
 * Format: "LOG:" (4 bytes) + total_length (4 bytes) + log data
 */
#define LOG_STREAM_MARKER       "LOG:"
#define LOG_STREAM_MARKER_LEN   4
#define LOG_STREAM_HEADER_SIZE  8

/* ========================================================================== */
/* State                                                                       */
/* ========================================================================== */

typedef struct {
    char        buffer[LOG_STREAM_BUFFER_SIZE];
    uint32_t    length;           /* Bytes used in buffer */
    uint8_t     streaming;        /* 1 if stream in progress */
    uint8_t     complete;         /* 1 if stream completed */
    uint8_t     overflow;         /* 1 if buffer overflowed */
    uint8_t     reserved;
} LogStreamState;

/* Global state (one instance per app) */
extern LogStreamState g_log_stream;

/* ========================================================================== */
/* API                                                                         */
/* ========================================================================== */

/**
 * Initialize log streaming
 *
 * Sets up PT_Log callback to capture logs to memory buffer.
 * Must be called after PT_LogCreate().
 *
 * @param log  PT_Log handle to capture from
 */
void log_stream_init(PT_Log *log);

/**
 * Start streaming logs to peer
 *
 * Initiates async stream of captured logs to the specified peer.
 * Use log_stream_complete() to check for completion.
 *
 * @param ctx      PeerTalk context
 * @param peer_id  Peer to stream logs to
 * @return PT_OK on success, error code on failure
 */
PeerTalk_Error log_stream_send(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id);

/**
 * Check if streaming is complete
 *
 * @return 1 if complete (success or failure), 0 if still streaming
 */
int log_stream_complete(void);

/**
 * Get streaming result
 *
 * Only valid after log_stream_complete() returns 1.
 *
 * @return PT_OK if successful, error code otherwise
 */
PeerTalk_Error log_stream_result(void);

/**
 * Get bytes sent so far
 */
uint32_t log_stream_bytes_sent(void);

/**
 * Cleanup log streaming state
 */
void log_stream_cleanup(void);

/* ========================================================================== */
/* Implementation (inline for single-file inclusion)                           */
/* ========================================================================== */

#ifdef LOG_STREAM_IMPLEMENTATION

#include <string.h>

/* Global state */
LogStreamState g_log_stream;

/* Stream completion callback state */
static PeerTalk_Error g_stream_result = PT_OK;
static uint32_t g_stream_bytes = 0;

/**
 * PT_Log callback - captures log messages to buffer
 */
static void log_stream_callback(
    PT_LogLevel     level,
    PT_LogCategory  category,
    uint32_t        timestamp_ms,
    const char     *message,
    void           *user_data)
{
    uint32_t msg_len;
    uint32_t remaining;

    (void)level;
    (void)category;
    (void)timestamp_ms;
    (void)user_data;

    if (g_log_stream.streaming) {
        /* Don't capture while streaming */
        return;
    }

    msg_len = strlen(message);
    /* Reserve space for header when calculating remaining */
    remaining = LOG_STREAM_BUFFER_SIZE - LOG_STREAM_HEADER_SIZE - g_log_stream.length;

    if (msg_len + 2 > remaining) {
        /* Buffer full - mark overflow and truncate */
        g_log_stream.overflow = 1;
        if (remaining > 2) {
            msg_len = remaining - 2;
        } else {
            return;
        }
    }

    /* Append message + newline */
    memcpy(g_log_stream.buffer + g_log_stream.length, message, msg_len);
    g_log_stream.length += msg_len;
    g_log_stream.buffer[g_log_stream.length++] = '\r';
    g_log_stream.buffer[g_log_stream.length++] = '\n';
}

/**
 * Stream completion callback
 */
static void log_stream_complete_cb(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID peer_id,
    uint32_t bytes_sent,
    PeerTalk_Error result,
    void *user_data)
{
    (void)ctx;
    (void)peer_id;
    (void)user_data;

    g_stream_bytes = bytes_sent;
    g_stream_result = result;
    g_log_stream.streaming = 0;
    g_log_stream.complete = 1;
}

void log_stream_init(PT_Log *log)
{
    memset(&g_log_stream, 0, sizeof(g_log_stream));
    g_stream_result = PT_OK;
    g_stream_bytes = 0;

    if (log) {
        /* Enable callback output and set our callback */
        uint8_t outputs = PT_LogGetOutput(log);
        PT_LogSetOutput(log, outputs | PT_LOG_OUT_CALLBACK);
        PT_LogSetCallback(log, log_stream_callback, NULL);
    }
}

PeerTalk_Error log_stream_send(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id)
{
    PeerTalk_Error err;
    uint32_t log_length;
    uint32_t total_length;

    if (g_log_stream.length == 0) {
        /* Nothing to send */
        g_log_stream.complete = 1;
        g_stream_result = PT_OK;
        return PT_OK;
    }

    /* Prepend header: "LOG:" + total_length
     * We need to shift buffer contents to make room for header.
     * Since we have LOG_STREAM_HEADER_SIZE bytes reserved, just shift.
     */
    log_length = g_log_stream.length;

    /* Shift log data to make room for header */
    memmove(g_log_stream.buffer + LOG_STREAM_HEADER_SIZE,
            g_log_stream.buffer,
            log_length);

    /* Write header */
    memcpy(g_log_stream.buffer, LOG_STREAM_MARKER, LOG_STREAM_MARKER_LEN);
    total_length = log_length + LOG_STREAM_HEADER_SIZE;
    memcpy(g_log_stream.buffer + LOG_STREAM_MARKER_LEN, &total_length, sizeof(total_length));

    g_log_stream.length = total_length;
    g_log_stream.streaming = 1;
    g_log_stream.complete = 0;
    g_stream_result = PT_OK;
    g_stream_bytes = 0;

    err = PeerTalk_StreamSend(ctx, peer_id,
                              g_log_stream.buffer,
                              g_log_stream.length,
                              log_stream_complete_cb,
                              NULL);

    if (err != PT_OK) {
        g_log_stream.streaming = 0;
        g_log_stream.complete = 1;
        g_stream_result = err;
    }

    return err;
}

int log_stream_complete(void)
{
    return g_log_stream.complete ? 1 : 0;
}

PeerTalk_Error log_stream_result(void)
{
    return g_stream_result;
}

uint32_t log_stream_bytes_sent(void)
{
    return g_stream_bytes;
}

void log_stream_cleanup(void)
{
    memset(&g_log_stream, 0, sizeof(g_log_stream));
}

#endif /* LOG_STREAM_IMPLEMENTATION */

#endif /* LOG_STREAM_H */
