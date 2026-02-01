/* protocol.c - Wire protocol implementation for PeerTalk */

#include "protocol.h"
#include "pt_internal.h"
#include "pt_compat.h"
#include "../../include/peertalk.h"

/* ========================================================================
 * CRC-16 Lookup Table
 * ======================================================================== */

/* CRC-16 table for polynomial 0x8408 (reflected 0x1021)
 * Generated with init=0x0000
 * Check value: pt_crc16("123456789", 9) == 0x2189
 */
static const uint16_t crc16_table[256] = {
    0x0000, 0x1189, 0x2312, 0x329B, 0x4624, 0x57AD, 0x6536, 0x74BF,
    0x8C48, 0x9DC1, 0xAF5A, 0xBED3, 0xCA6C, 0xDBE5, 0xE97E, 0xF8F7,
    0x1081, 0x0108, 0x3393, 0x221A, 0x56A5, 0x472C, 0x75B7, 0x643E,
    0x9CC9, 0x8D40, 0xBFDB, 0xAE52, 0xDAED, 0xCB64, 0xF9FF, 0xE876,
    0x2102, 0x308B, 0x0210, 0x1399, 0x6726, 0x76AF, 0x4434, 0x55BD,
    0xAD4A, 0xBCC3, 0x8E58, 0x9FD1, 0xEB6E, 0xFAE7, 0xC87C, 0xD9F5,
    0x3183, 0x200A, 0x1291, 0x0318, 0x77A7, 0x662E, 0x54B5, 0x453C,
    0xBDCB, 0xAC42, 0x9ED9, 0x8F50, 0xFBEF, 0xEA66, 0xD8FD, 0xC974,
    0x4204, 0x538D, 0x6116, 0x709F, 0x0420, 0x15A9, 0x2732, 0x36BB,
    0xCE4C, 0xDFC5, 0xED5E, 0xFCD7, 0x8868, 0x99E1, 0xAB7A, 0xBAF3,
    0x5285, 0x430C, 0x7197, 0x601E, 0x14A1, 0x0528, 0x37B3, 0x263A,
    0xDECD, 0xCF44, 0xFDDF, 0xEC56, 0x98E9, 0x8960, 0xBBFB, 0xAA72,
    0x6306, 0x728F, 0x4014, 0x519D, 0x2522, 0x34AB, 0x0630, 0x17B9,
    0xEF4E, 0xFEC7, 0xCC5C, 0xDDD5, 0xA96A, 0xB8E3, 0x8A78, 0x9BF1,
    0x7387, 0x620E, 0x5095, 0x411C, 0x35A3, 0x242A, 0x16B1, 0x0738,
    0xFFCF, 0xEE46, 0xDCDD, 0xCD54, 0xB9EB, 0xA862, 0x9AF9, 0x8B70,
    0x8408, 0x9581, 0xA71A, 0xB693, 0xC22C, 0xD3A5, 0xE13E, 0xF0B7,
    0x0840, 0x19C9, 0x2B52, 0x3ADB, 0x4E64, 0x5FED, 0x6D76, 0x7CFF,
    0x9489, 0x8500, 0xB79B, 0xA612, 0xD2AD, 0xC324, 0xF1BF, 0xE036,
    0x18C1, 0x0948, 0x3BD3, 0x2A5A, 0x5EE5, 0x4F6C, 0x7DF7, 0x6C7E,
    0xA50A, 0xB483, 0x8618, 0x9791, 0xE32E, 0xF2A7, 0xC03C, 0xD1B5,
    0x2942, 0x38CB, 0x0A50, 0x1BD9, 0x6F66, 0x7EEF, 0x4C74, 0x5DFD,
    0xB58B, 0xA402, 0x9699, 0x8710, 0xF3AF, 0xE226, 0xD0BD, 0xC134,
    0x39C3, 0x284A, 0x1AD1, 0x0B58, 0x7FE7, 0x6E6E, 0x5CF5, 0x4D7C,
    0xC60C, 0xD785, 0xE51E, 0xF497, 0x8028, 0x91A1, 0xA33A, 0xB2B3,
    0x4A44, 0x5BCD, 0x6956, 0x78DF, 0x0C60, 0x1DE9, 0x2F72, 0x3EFB,
    0xD68D, 0xC704, 0xF59F, 0xE416, 0x90A9, 0x8120, 0xB3BB, 0xA232,
    0x5AC5, 0x4B4C, 0x79D7, 0x685E, 0x1CE1, 0x0D68, 0x3FF3, 0x2E7A,
    0xE70E, 0xF687, 0xC41C, 0xD595, 0xA12A, 0xB0A3, 0x8238, 0x93B1,
    0x6B46, 0x7ACF, 0x4854, 0x59DD, 0x2D62, 0x3CEB, 0x0E70, 0x1FF9,
    0xF78F, 0xE606, 0xD49D, 0xC514, 0xB1AB, 0xA022, 0x92B9, 0x8330,
    0x7BC7, 0x6A4E, 0x58D5, 0x495C, 0x3DE3, 0x2C6A, 0x1EF1, 0x0F78
};

/* ========================================================================
 * CRC-16 Functions
 * ======================================================================== */

uint16_t pt_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0x0000;
    while (len--) {
        crc = (crc >> 8) ^ crc16_table[(crc ^ *data++) & 0xFF];
    }
    return crc;
}

uint16_t pt_crc16_update(uint16_t crc, const uint8_t *data, size_t len)
{
    while (len--) {
        crc = (crc >> 8) ^ crc16_table[(crc ^ *data++) & 0xFF];
    }
    return crc;
}

int pt_crc16_check(const uint8_t *data, size_t len, uint16_t expected)
{
    return pt_crc16(data, len) == expected ? 1 : 0;
}

/* ========================================================================
 * Discovery Packet Functions
 * ======================================================================== */

int pt_discovery_encode(const pt_discovery_packet *pkt, uint8_t *buf,
                        size_t buf_len)
{
    uint16_t crc;
    size_t packet_size;

    /* Validate name length */
    if (pkt->name_len > PT_PEER_NAME_MAX) {
        return PT_ERR_INVALID;
    }

    /* Validate type */
    if (pkt->type < PT_DISC_TYPE_ANNOUNCE || pkt->type > PT_DISC_TYPE_GOODBYE) {
        return PT_ERR_INVALID;
    }

    /* Calculate packet size */
    packet_size = 12 + pkt->name_len + 2;  /* header + name + CRC */

    /* Check buffer size */
    if (buf_len < packet_size) {
        return PT_ERR_BUFFER_FULL;
    }

    /* Magic: "PTLK" */
    buf[0] = 'P';
    buf[1] = 'T';
    buf[2] = 'L';
    buf[3] = 'K';

    /* Version, Type */
    buf[4] = pkt->version;
    buf[5] = pkt->type;

    /* Flags (big-endian) */
    buf[6] = (pkt->flags >> 8) & 0xFF;
    buf[7] = pkt->flags & 0xFF;

    /* Sender Port (big-endian) */
    buf[8] = (pkt->sender_port >> 8) & 0xFF;
    buf[9] = pkt->sender_port & 0xFF;

    /* Transports, Name Length */
    buf[10] = pkt->transports;
    buf[11] = pkt->name_len;

    /* Peer Name */
    pt_memcpy(buf + 12, pkt->name, pkt->name_len);

    /* CRC-16 over header + name */
    crc = pt_crc16(buf, 12 + pkt->name_len);
    buf[12 + pkt->name_len] = (crc >> 8) & 0xFF;
    buf[12 + pkt->name_len + 1] = crc & 0xFF;

    return (int)packet_size;
}

int pt_discovery_decode(struct pt_context *ctx, const uint8_t *buf, size_t len,
                        pt_discovery_packet *pkt)
{
    uint16_t crc_computed, crc_received;
    size_t expected_len;

    /* Minimum size check */
    if (len < 14) {
        if (ctx) {
            PT_CTX_WARN(ctx, PT_LOG_CAT_PROTOCOL,
                        "Discovery packet too short: %zu bytes (min 14)", len);
        }
        return PT_ERR_TRUNCATED;
    }

    /* Magic validation */
    if (buf[0] != 'P' || buf[1] != 'T' || buf[2] != 'L' || buf[3] != 'K') {
        if (ctx) {
            PT_CTX_WARN(ctx, PT_LOG_CAT_PROTOCOL,
                        "Invalid discovery magic: 0x%02X%02X%02X%02X",
                        buf[0], buf[1], buf[2], buf[3]);
        }
        return PT_ERR_MAGIC;
    }

    /* Extract version */
    pkt->version = buf[4];
    if (pkt->version != PT_PROTOCOL_VERSION) {
        if (ctx) {
            PT_CTX_WARN(ctx, PT_LOG_CAT_PROTOCOL,
                    "Protocol version mismatch: got %u, expected %u",
                    pkt->version, PT_PROTOCOL_VERSION);
        }
        return PT_ERR_VERSION;
    }

    /* Extract type */
    pkt->type = buf[5];
    if (pkt->type < PT_DISC_TYPE_ANNOUNCE || pkt->type > PT_DISC_TYPE_GOODBYE) {
        if (ctx) {
            PT_CTX_WARN(ctx, PT_LOG_CAT_PROTOCOL,
                    "Invalid discovery type: 0x%02X", pkt->type);
        }
        return PT_ERR_INVALID;
    }

    /* Extract flags (big-endian) */
    pkt->flags = ((uint16_t)buf[6] << 8) | buf[7];

    /* Extract sender port (big-endian) */
    pkt->sender_port = ((uint16_t)buf[8] << 8) | buf[9];

    /* Extract transports and name length */
    pkt->transports = buf[10];
    pkt->name_len = buf[11];

    /* Validate name length */
    if (pkt->name_len > PT_PEER_NAME_MAX) {
        if (ctx) {
            PT_CTX_WARN(ctx, PT_LOG_CAT_PROTOCOL,
                    "Name length too long: %u (max %u)",
                    pkt->name_len, PT_PEER_NAME_MAX);
        }
        return PT_ERR_INVALID;
    }

    /* Check complete packet length */
    expected_len = 12 + pkt->name_len + 2;
    if (len < expected_len) {
        if (ctx) {
            PT_CTX_WARN(ctx, PT_LOG_CAT_PROTOCOL,
                    "Discovery packet truncated: %zu bytes (expected %zu)",
                    len, expected_len);
        }
        return PT_ERR_TRUNCATED;
    }

    /* Extract name */
    pt_memcpy(pkt->name, buf + 12, pkt->name_len);
    pkt->name[pkt->name_len] = '\0';  /* Null-terminate */

    /* Extract CRC (big-endian) */
    crc_received = ((uint16_t)buf[12 + pkt->name_len] << 8) |
                   buf[12 + pkt->name_len + 1];

    /* Verify CRC */
    crc_computed = pt_crc16(buf, 12 + pkt->name_len);
    if (crc_computed != crc_received) {
        if (ctx) {
            PT_CTX_WARN(ctx, PT_LOG_CAT_PROTOCOL,
                    "Discovery CRC mismatch: got 0x%04X, expected 0x%04X",
                    crc_received, crc_computed);
        }
        return PT_ERR_CRC;
    }

    if (ctx) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_PROTOCOL,
                     "Discovery packet decoded: type=%u, name='%s', port=%u",
                     pkt->type, pkt->name, pkt->sender_port);
    }

    return 0;
}

/* ========================================================================
 * Message Frame Functions
 * ======================================================================== */

int pt_message_encode_header(const pt_message_header *hdr, uint8_t *buf)
{
    /* Magic: "PTMG" */
    buf[0] = 'P';
    buf[1] = 'T';
    buf[2] = 'M';
    buf[3] = 'G';

    /* Version, Type, Flags, Sequence */
    buf[4] = hdr->version;
    buf[5] = hdr->type;
    buf[6] = hdr->flags;
    buf[7] = hdr->sequence;

    /* Payload Length (big-endian) */
    buf[8] = (hdr->payload_len >> 8) & 0xFF;
    buf[9] = hdr->payload_len & 0xFF;

    return PT_MESSAGE_HEADER_SIZE;
}

int pt_message_decode_header(struct pt_context *ctx, const uint8_t *buf,
                             size_t len, pt_message_header *hdr)
{
    /* Minimum size check */
    if (len < PT_MESSAGE_HEADER_SIZE) {
        if (ctx) {
            PT_CTX_WARN(ctx, PT_LOG_CAT_PROTOCOL,
                    "Message header too short: %zu bytes (min %d)",
                    len, PT_MESSAGE_HEADER_SIZE);
        }
        return PT_ERR_TRUNCATED;
    }

    /* Magic validation */
    if (buf[0] != 'P' || buf[1] != 'T' || buf[2] != 'M' || buf[3] != 'G') {
        if (ctx) {
            PT_CTX_WARN(ctx, PT_LOG_CAT_PROTOCOL,
                    "Invalid message magic: 0x%02X%02X%02X%02X",
                    buf[0], buf[1], buf[2], buf[3]);
        }
        return PT_ERR_MAGIC;
    }

    /* Extract version */
    hdr->version = buf[4];
    if (hdr->version != PT_PROTOCOL_VERSION) {
        if (ctx) {
            PT_CTX_WARN(ctx, PT_LOG_CAT_PROTOCOL,
                    "Protocol version mismatch: got %u, expected %u",
                    hdr->version, PT_PROTOCOL_VERSION);
        }
        return PT_ERR_VERSION;
    }

    /* Extract type */
    hdr->type = buf[5];
    if (hdr->type < PT_MSG_TYPE_DATA || hdr->type > PT_MSG_TYPE_REJECT) {
        if (ctx) {
            PT_CTX_WARN(ctx, PT_LOG_CAT_PROTOCOL,
                    "Invalid message type: 0x%02X", hdr->type);
        }
        return PT_ERR_INVALID;
    }

    /* Extract flags, sequence */
    hdr->flags = buf[6];
    hdr->sequence = buf[7];

    /* Extract payload length (big-endian) */
    hdr->payload_len = ((uint16_t)buf[8] << 8) | buf[9];

    if (ctx) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_PROTOCOL,
                     "Message header decoded: type=%u, seq=%u, len=%u",
                     hdr->type, hdr->sequence, hdr->payload_len);
    }

    return 0;
}

/* ========================================================================
 * UDP Message Functions
 * ======================================================================== */

int pt_udp_encode(const void *payload, uint16_t payload_len,
                  uint16_t sender_port, uint8_t *buf, size_t buf_len)
{
    size_t packet_size = PT_UDP_HEADER_SIZE + payload_len;

    /* Check buffer size */
    if (buf_len < packet_size) {
        return PT_ERR_BUFFER_FULL;
    }

    /* Magic: "PTUD" */
    buf[0] = 'P';
    buf[1] = 'T';
    buf[2] = 'U';
    buf[3] = 'D';

    /* Sender Port (big-endian) */
    buf[4] = (sender_port >> 8) & 0xFF;
    buf[5] = sender_port & 0xFF;

    /* Payload Length (big-endian) */
    buf[6] = (payload_len >> 8) & 0xFF;
    buf[7] = payload_len & 0xFF;

    /* Payload */
    if (payload_len > 0) {
        pt_memcpy(buf + PT_UDP_HEADER_SIZE, payload, payload_len);
    }

    return (int)packet_size;
}

int pt_udp_decode(struct pt_context *ctx, const uint8_t *buf, size_t len,
                  uint16_t *sender_port_out, const void **payload_out,
                  uint16_t *payload_len_out)
{
    uint16_t payload_len;

    /* Minimum size check */
    if (len < PT_UDP_HEADER_SIZE) {
        if (ctx) {
            PT_CTX_WARN(ctx, PT_LOG_CAT_PROTOCOL,
                    "UDP message too short: %zu bytes (min %d)",
                    len, PT_UDP_HEADER_SIZE);
        }
        return PT_ERR_TRUNCATED;
    }

    /* Magic validation */
    if (buf[0] != 'P' || buf[1] != 'T' || buf[2] != 'U' || buf[3] != 'D') {
        if (ctx) {
            PT_CTX_WARN(ctx, PT_LOG_CAT_PROTOCOL,
                    "Invalid UDP magic: 0x%02X%02X%02X%02X",
                    buf[0], buf[1], buf[2], buf[3]);
        }
        return PT_ERR_MAGIC;
    }

    /* Extract sender port (big-endian) */
    *sender_port_out = ((uint16_t)buf[4] << 8) | buf[5];

    /* Extract payload length (big-endian) */
    payload_len = ((uint16_t)buf[6] << 8) | buf[7];
    *payload_len_out = payload_len;

    /* Verify complete packet length */
    if (len < PT_UDP_HEADER_SIZE + payload_len) {
        if (ctx) {
            PT_CTX_WARN(ctx, PT_LOG_CAT_PROTOCOL,
                    "UDP packet truncated: %zu bytes (expected %u)",
                    len, PT_UDP_HEADER_SIZE + payload_len);
        }
        return PT_ERR_TRUNCATED;
    }

    /* Set payload pointer */
    *payload_out = buf + PT_UDP_HEADER_SIZE;

    if (ctx) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_PROTOCOL,
                     "UDP message decoded: port=%u, len=%u",
                     *sender_port_out, payload_len);
    }

    return 0;
}
