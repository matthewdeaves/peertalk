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

/* Forward declarations */
struct pt_context;
struct pt_peer;

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
#define PT_MSG_TYPE_CAPABILITY  0x07

/* Discovery transport flags (bitmask) */
#define PT_DISC_TRANSPORT_TCP       0x01
#define PT_DISC_TRANSPORT_UDP       0x02
#define PT_DISC_TRANSPORT_APPLETALK 0x04

/* Discovery flags (match PT_PEER_FLAG_* from peertalk.h) */
#define PT_DISC_FLAG_HOST       0x0001
#define PT_DISC_FLAG_ACCEPTING  0x0002
#define PT_DISC_FLAG_SPECTATOR  0x0004
#define PT_DISC_FLAG_READY      0x0008
#define PT_DISC_FLAG_HAS_CAPS   0x0010  /* Peer supports capability exchange */

/* Message flags (match PT_SEND_* from peertalk.h) */
#define PT_MSG_FLAG_UNRELIABLE  0x01
#define PT_MSG_FLAG_COALESCABLE 0x02
#define PT_MSG_FLAG_NO_DELAY    0x04
#define PT_MSG_FLAG_BATCH       0x08
#define PT_MSG_FLAG_FRAGMENT    0x10  /* Message is fragmented */

/* ========================================================================
 * Capability Negotiation Protocol
 * ======================================================================== */

/* Capability TLV types (for PT_MSG_TYPE_CAPABILITY payload) */
#define PT_CAP_MAX_MESSAGE      0x01  /* 2 bytes: max efficient message size */
#define PT_CAP_PREFERRED_CHUNK  0x02  /* 2 bytes: optimal streaming chunk */
#define PT_CAP_BUFFER_PRESSURE  0x03  /* 1 byte: 0-100 constraint level */
#define PT_CAP_FLAGS            0x04  /* 2 bytes: capability flags */

/* Capability flags (sent in PT_CAP_FLAGS TLV) */
#define PT_CAPFLAG_FRAGMENTATION 0x0001  /* Peer supports fragmentation */
#define PT_CAPFLAG_STREAMING     0x0002  /* Peer supports streaming */

/* Capability defaults (for legacy peers without PT_DISC_FLAG_HAS_CAPS) */
#define PT_CAP_DEFAULT_MAX_MSG      512     /* Conservative for legacy */
#define PT_CAP_DEFAULT_CHUNK        256     /* Conservative for legacy */
#define PT_CAP_DEFAULT_PRESSURE     50      /* Moderate constraint */

/* Capability limits */
#define PT_CAP_MIN_MAX_MSG          256     /* Minimum supported */
#define PT_CAP_MAX_MAX_MSG          8192    /* Maximum supported */

/* Fragment header constants */
#define PT_FRAGMENT_HEADER_SIZE     8       /* Size of fragment header */
#define PT_FRAGMENT_FLAG_FIRST      0x01    /* First fragment */
#define PT_FRAGMENT_FLAG_LAST       0x02    /* Last fragment */

/* ========================================================================
 * Data Structures
 * ======================================================================== */

/* Fragment header (prepended to each fragment)
 *
 * Wire format (8 bytes):
 * - Message ID (2, big-endian): Links fragments together
 * - Total Length (2, big-endian): Original message size
 * - Fragment Offset (2, big-endian): Byte offset in original
 * - Fragment Flags (1): FIRST=0x01, LAST=0x02
 * - Reserved (1)
 */
typedef struct {
    uint16_t message_id;        /* Links fragments together */
    uint16_t total_length;      /* Original message size */
    uint16_t fragment_offset;   /* Byte offset in original */
    uint8_t  fragment_flags;    /* PT_FRAGMENT_FLAG_* */
    uint8_t  reserved;
} pt_fragment_header;

/* Capability message (for parsing PT_MSG_TYPE_CAPABILITY)
 *
 * Sent after TCP connection established. Peers exchange capabilities
 * and negotiate effective_max_msg = min(local, remote).
 */
typedef struct {
    uint16_t max_message_size;   /* Max efficient message size (256-8192) */
    uint16_t preferred_chunk;    /* Optimal streaming chunk size */
    uint16_t capability_flags;   /* PT_CAPFLAG_* */
    uint8_t  buffer_pressure;    /* 0-100 constraint level */
    uint8_t  reserved;
} pt_capability_msg;

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

/* ========================================================================
 * Capability Message Functions
 * ======================================================================== */

/* Encode capability message to wire format (TLV encoding)
 *
 * Args:
 *   caps    - Capability message to encode
 *   buf     - Output buffer
 *   buf_len - Buffer size
 *
 * Returns: Payload size on success, negative error code on failure
 */
int pt_capability_encode(const pt_capability_msg *caps, uint8_t *buf, size_t buf_len);

/* Decode capability message from wire format (TLV decoding)
 *
 * Args:
 *   ctx     - Context for logging (can be NULL)
 *   buf     - Wire format data (payload only, no header)
 *   len     - Data length
 *   caps    - Output parsed capability message
 *
 * Returns: 0 on success, negative error code on failure
 */
int pt_capability_decode(struct pt_context *ctx, const uint8_t *buf, size_t len,
                         pt_capability_msg *caps);

/* ========================================================================
 * Fragment Header Functions
 * ======================================================================== */

/* Encode fragment header to wire format
 *
 * Args:
 *   hdr - Fragment header to encode
 *   buf - Output buffer (at least PT_FRAGMENT_HEADER_SIZE bytes)
 *
 * Returns: PT_FRAGMENT_HEADER_SIZE on success
 */
int pt_fragment_encode(const pt_fragment_header *hdr, uint8_t *buf);

/* Decode fragment header from wire format
 *
 * Args:
 *   buf - Wire format data
 *   len - Data length
 *   hdr - Output parsed fragment header
 *
 * Returns: 0 on success, negative error code on failure
 */
int pt_fragment_decode(const uint8_t *buf, size_t len, pt_fragment_header *hdr);

/* ========================================================================
 * Fragment Reassembly API
 *
 * These functions handle transparent fragment reassembly. Applications
 * never see fragments - the SDK accumulates them and delivers complete
 * messages to the callback.
 * ======================================================================== */

/* Process a received fragment
 *
 * Accumulates fragment data in peer's recv_direct buffer. When the last
 * fragment arrives, returns the complete reassembled message.
 *
 * Args:
 *   ctx           - PeerTalk context
 *   peer          - Peer that sent the fragment
 *   fragment_data - Raw fragment data (includes fragment header)
 *   fragment_len  - Length of fragment data
 *   frag_hdr      - Decoded fragment header
 *   complete_data - OUTPUT: pointer to complete message data (if ready)
 *   complete_len  - OUTPUT: length of complete message (if ready)
 *
 * Returns:
 *   1 - Complete message ready (check complete_data/complete_len)
 *   0 - Fragment received, more expected
 *   <0 - Error code (PT_ERR_*)
 */
int pt_reassembly_process(struct pt_context *ctx, struct pt_peer *peer,
                          const uint8_t *fragment_data, uint16_t fragment_len,
                          const pt_fragment_header *frag_hdr,
                          const uint8_t **complete_data, uint16_t *complete_len);

/* Reset reassembly state for a peer
 *
 * Called on disconnect or error to clean up partial reassembly.
 */
void pt_reassembly_reset(struct pt_peer *peer);

#endif /* PT_PROTOCOL_H */
