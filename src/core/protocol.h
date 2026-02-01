/* protocol.h - Wire protocol definitions for PeerTalk
 *
 * Implements framing for:
 * - Discovery packets (UDP broadcast)
 * - Message frames (TCP streaming)
 * - Unreliable messages (UDP)
 */

#ifndef PT_PROTOCOL_H
#define PT_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

/* Forward declaration */
struct pt_context;

/* ========================================================================
 * Protocol Constants
 * ======================================================================== */

#define PT_PROTOCOL_VERSION 1

/* Magic numbers for packet identification */
#define PT_MAGIC_DISCOVERY  0x50544C4B  /* "PTLK" */
#define PT_MAGIC_MESSAGE    0x50544D47  /* "PTMG" */
#define PT_MAGIC_UDP        0x50545544  /* "PTUD" */

/* Maximum sizes */
#define PT_DISCOVERY_MAX_SIZE   48
#define PT_PEER_NAME_MAX        31
#define PT_MESSAGE_MAX_PAYLOAD  65535
#define PT_MESSAGE_HEADER_SIZE  10
#define PT_UDP_HEADER_SIZE      8

/* Discovery packet types */
#define PT_DISC_TYPE_ANNOUNCE   0x01
#define PT_DISC_TYPE_QUERY      0x02
#define PT_DISC_TYPE_GOODBYE    0x03

/* Message types */
#define PT_MSG_TYPE_DATA        0x01
#define PT_MSG_TYPE_PING        0x02
#define PT_MSG_TYPE_PONG        0x03
#define PT_MSG_TYPE_DISCONNECT  0x04
#define PT_MSG_TYPE_ACK         0x05
#define PT_MSG_TYPE_REJECT      0x06

/* Discovery transport flags (bitmask) */
#define PT_DISC_TRANSPORT_TCP       0x01
#define PT_DISC_TRANSPORT_UDP       0x02
#define PT_DISC_TRANSPORT_APPLETALK 0x04

/* Discovery flags (match PT_PEER_FLAG_* from peertalk.h) */
#define PT_DISC_FLAG_HOST       0x0001
#define PT_DISC_FLAG_ACCEPTING  0x0002
#define PT_DISC_FLAG_SPECTATOR  0x0004
#define PT_DISC_FLAG_READY      0x0008

/* Message flags (match PT_SEND_* from peertalk.h) */
#define PT_MSG_FLAG_UNRELIABLE  0x01
#define PT_MSG_FLAG_COALESCABLE 0x02
#define PT_MSG_FLAG_NO_DELAY    0x04
#define PT_MSG_FLAG_BATCH       0x08

/* ========================================================================
 * Data Structures
 * ======================================================================== */

/* Parsed discovery packet
 *
 * Wire format (14-45 bytes):
 * - Magic (4): "PTLK"
 * - Version (1), Type (1), Flags (2, big-endian)
 * - Sender Port (2, big-endian), Transports (1), Name Len (1)
 * - Peer Name (up to 31 bytes)
 * - CRC-16 (2, big-endian)
 */
typedef struct {
    uint8_t  version;
    uint8_t  type;              /* PT_DISC_TYPE_* */
    uint16_t flags;             /* PT_DISC_FLAG_* (matches PT_PEER_FLAG_*) */
    uint16_t sender_port;
    uint8_t  transports;        /* PT_DISC_TRANSPORT_* bitmask */
    uint8_t  name_len;
    char     name[PT_PEER_NAME_MAX + 1];  /* Null-terminated */
} pt_discovery_packet;

/* Parsed message header
 *
 * Wire format (10 bytes):
 * - Magic (4): "PTMG"
 * - Version (1), Type (1), Flags (1), Sequence (1)
 * - Payload Length (2, big-endian)
 *
 * Note: Payload and CRC-16 trailer follow the header
 */
typedef struct {
    uint8_t  version;
    uint8_t  type;              /* PT_MSG_TYPE_* */
    uint8_t  flags;             /* PT_MSG_FLAG_* (matches PT_SEND_*) */
    uint8_t  sequence;
    uint16_t payload_len;
} pt_message_header;

/* ========================================================================
 * CRC-16 Functions
 * ======================================================================== */

/* Compute CRC-16 over data
 *
 * Algorithm: Polynomial 0x1021 (reflected 0x8408)
 * Init: 0x0000 (not 0xFFFF)
 * Check value: pt_crc16("123456789", 9) == 0x2189
 *
 * Args:
 *   data - Data to checksum
 *   len  - Length in bytes
 *
 * Returns: CRC-16 value (store as big-endian in wire format)
 */
uint16_t pt_crc16(const uint8_t *data, size_t len);

/* Update CRC-16 with additional data
 *
 * Used for non-contiguous data (e.g., header + payload):
 *   crc = pt_crc16(header, header_len);
 *   crc = pt_crc16_update(crc, payload, payload_len);
 *
 * Args:
 *   crc  - Previous CRC value
 *   data - Additional data to checksum
 *   len  - Length in bytes
 *
 * Returns: Updated CRC-16 value
 */
uint16_t pt_crc16_update(uint16_t crc, const uint8_t *data, size_t len);

/* Verify CRC-16 matches expected value
 *
 * Args:
 *   data     - Data to checksum
 *   len      - Length in bytes
 *   expected - Expected CRC value
 *
 * Returns: 1 if CRC matches, 0 if mismatch
 */
int pt_crc16_check(const uint8_t *data, size_t len, uint16_t expected);

/* ========================================================================
 * Discovery Packet Functions
 * ======================================================================== */

/* Encode discovery packet to wire format
 *
 * Args:
 *   pkt     - Parsed discovery packet
 *   buf     - Output buffer (at least PT_DISCOVERY_MAX_SIZE bytes)
 *   buf_len - Buffer size
 *
 * Returns: Packet size in bytes on success, negative error code on failure
 *   PT_ERR_INVALID if name_len > 31 or type is invalid
 *   PT_ERR_BUFFER_FULL if buf_len is insufficient
 */
int pt_discovery_encode(const pt_discovery_packet *pkt, uint8_t *buf,
                        size_t buf_len);

/* Decode discovery packet from wire format
 *
 * Validates:
 * - Magic number ("PTLK")
 * - Version compatibility
 * - Packet length (minimum 14 bytes)
 * - Name length (≤31)
 * - Type (0x01-0x03)
 * - CRC-16
 *
 * Args:
 *   ctx - Context for logging (can be NULL for no logging)
 *   buf - Wire format data
 *   len - Data length
 *   pkt - Output parsed packet (name is null-terminated)
 *
 * Returns: 0 on success, negative error code on failure
 *   PT_ERR_MAGIC if magic is incorrect
 *   PT_ERR_VERSION if version mismatch
 *   PT_ERR_TRUNCATED if packet is too short
 *   PT_ERR_INVALID if name_len > 31 or type invalid
 *   PT_ERR_CRC if checksum fails
 */
int pt_discovery_decode(struct pt_context *ctx, const uint8_t *buf, size_t len,
                        pt_discovery_packet *pkt);

/* ========================================================================
 * Message Frame Functions
 * ======================================================================== */

/* Encode message header to wire format
 *
 * Note: Caller must append payload and CRC-16 trailer
 *
 * Args:
 *   hdr - Parsed message header
 *   buf - Output buffer (at least PT_MESSAGE_HEADER_SIZE bytes)
 *
 * Returns: PT_MESSAGE_HEADER_SIZE on success
 */
int pt_message_encode_header(const pt_message_header *hdr, uint8_t *buf);

/* Decode message header from wire format
 *
 * Validates:
 * - Magic number ("PTMG")
 * - Version compatibility
 * - Packet length (minimum 10 bytes)
 * - Type (0x01-0x06)
 *
 * Note: Does NOT validate CRC (caller must verify header + payload + CRC)
 *
 * Args:
 *   ctx - Context for logging (can be NULL for no logging)
 *   buf - Wire format data
 *   len - Data length
 *   hdr - Output parsed header
 *
 * Returns: 0 on success, negative error code on failure
 *   PT_ERR_MAGIC if magic is incorrect
 *   PT_ERR_VERSION if version mismatch
 *   PT_ERR_TRUNCATED if packet is too short
 *   PT_ERR_INVALID if type is invalid
 */
int pt_message_decode_header(struct pt_context *ctx, const uint8_t *buf,
                             size_t len, pt_message_header *hdr);

/* ========================================================================
 * UDP Message Functions
 * ======================================================================== */

/* Encode UDP message to wire format
 *
 * Wire format (8 + payload_len bytes):
 * - Magic (4): "PTUD"
 * - Sender Port (2, big-endian)
 * - Payload Length (2, big-endian)
 * - Payload (0-65535 bytes)
 *
 * Note: No CRC (UDP has its own checksum)
 *
 * Args:
 *   payload     - Message payload
 *   payload_len - Payload length
 *   sender_port - Sender's UDP port
 *   buf         - Output buffer
 *   buf_len     - Buffer size
 *
 * Returns: Packet size on success, negative error code on failure
 *   PT_ERR_BUFFER_FULL if buf_len is insufficient
 */
int pt_udp_encode(const void *payload, uint16_t payload_len,
                  uint16_t sender_port, uint8_t *buf, size_t buf_len);

/* Decode UDP message from wire format
 *
 * Validates:
 * - Magic number ("PTUD")
 * - Packet length (minimum 8 bytes)
 *
 * Args:
 *   ctx             - Context for logging (can be NULL)
 *   buf             - Wire format data
 *   len             - Data length
 *   sender_port_out - Output sender port
 *   payload_out     - Output payload pointer (points into buf)
 *   payload_len_out - Output payload length
 *
 * Returns: 0 on success, negative error code on failure
 *   PT_ERR_MAGIC if magic is incorrect
 *   PT_ERR_TRUNCATED if packet is too short
 */
int pt_udp_decode(struct pt_context *ctx, const uint8_t *buf, size_t len,
                  uint16_t *sender_port_out, const void **payload_out,
                  uint16_t *payload_len_out);

#endif /* PT_PROTOCOL_H */
