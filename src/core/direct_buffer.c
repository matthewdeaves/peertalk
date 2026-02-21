/* direct_buffer.c - Tier 2 direct buffers for large messages
 *
 * Implements pre-allocated buffers for messages > 256 bytes.
 * One buffer per peer per direction avoids memory fragmentation
 * and provides predictable memory usage on Classic Mac.
 */

#include "direct_buffer.h"
#include "pt_compat.h"
#include "../../include/peertalk.h"

/* ========================================================================
 * Buffer Lifecycle
 * ======================================================================== */

int pt_direct_buffer_init(pt_direct_buffer *buf, uint16_t capacity)
{
    if (!buf) {
        return PT_ERR_INVALID_PARAM;
    }

    /* Validate capacity */
    if (capacity == 0) {
        capacity = PT_DIRECT_DEFAULT_SIZE;
    }
    if (capacity > PT_DIRECT_MAX_SIZE) {
        return PT_ERR_INVALID_PARAM;
    }

    /* Allocate data buffer */
    buf->data = (uint8_t *)pt_alloc(capacity);
    if (!buf->data) {
        return PT_ERR_NO_MEMORY;
    }

    /* Initialize state */
    buf->state = PT_DIRECT_IDLE;
    buf->length = 0;
    buf->capacity = capacity;
    buf->priority = PT_PRIORITY_NORMAL;
    buf->msg_flags = 0;

    return PT_OK;
}

void pt_direct_buffer_free(pt_direct_buffer *buf)
{
    if (!buf) {
        return;
    }

    if (buf->data) {
        pt_free(buf->data);
        buf->data = NULL;
    }

    buf->state = PT_DIRECT_IDLE;
    buf->length = 0;
    buf->capacity = 0;
}

/* ========================================================================
 * Send Path Operations
 * ======================================================================== */

int pt_direct_buffer_queue(pt_direct_buffer *buf, const void *data,
                           uint16_t length, uint8_t priority)
{
    if (!buf || !data || length == 0) {
        return PT_ERR_INVALID_PARAM;
    }

    /* Check if buffer is available */
    if (buf->state != PT_DIRECT_IDLE) {
        return PT_ERR_WOULD_BLOCK;
    }

    /* Check message size */
    if (length > buf->capacity) {
        return PT_ERR_MESSAGE_TOO_LARGE;
    }

    /* Copy data to buffer */
    pt_memcpy(buf->data, data, length);
    buf->length = length;
    buf->priority = priority;
    buf->msg_flags = 0;  /* Caller can override for fragments */

    /* Mark as queued - atomic write last for visibility */
    buf->state = PT_DIRECT_QUEUED;

    return PT_OK;
}

int pt_direct_buffer_mark_sending(pt_direct_buffer *buf)
{
    if (!buf) {
        return -1;
    }

    if (buf->state != PT_DIRECT_QUEUED) {
        return -1;
    }

    buf->state = PT_DIRECT_SENDING;
    return 0;
}

void pt_direct_buffer_complete(pt_direct_buffer *buf)
{
    if (!buf) {
        return;
    }

    /* Reset to idle - buffer available for next message */
    buf->length = 0;
    buf->state = PT_DIRECT_IDLE;
}

int pt_direct_buffer_ready(const pt_direct_buffer *buf)
{
    if (!buf) {
        return 0;
    }
    return (buf->state == PT_DIRECT_QUEUED) ? 1 : 0;
}

int pt_direct_buffer_available(const pt_direct_buffer *buf)
{
    if (!buf) {
        return 0;
    }
    return (buf->state == PT_DIRECT_IDLE) ? 1 : 0;
}

/* ========================================================================
 * Receive Path Operations
 * ======================================================================== */

int pt_direct_buffer_receive(pt_direct_buffer *buf, const void *data,
                             uint16_t length)
{
    if (!buf || !data) {
        return PT_ERR_INVALID_PARAM;
    }

    /* Check message size */
    if (length > buf->capacity) {
        return PT_ERR_MESSAGE_TOO_LARGE;
    }

    /* Copy incoming data
     * Note: For receive path, we don't track state - data is delivered
     * immediately to application callback. Buffer is just temporary storage.
     */
    pt_memcpy(buf->data, data, length);
    buf->length = length;

    return PT_OK;
}
