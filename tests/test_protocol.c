/* test_protocol.c - Tests for wire protocol framing */

#include "../src/core/protocol.h"
#include "../include/peertalk.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Test counter */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    do { \
        printf("Running %s... ", name); \
        fflush(stdout); \
    } while (0)

#define PASS() \
    do { \
        printf("PASS\n"); \
        tests_passed++; \
    } while (0)

#define FAIL(msg, ...) \
    do { \
        printf("FAIL: "); \
        printf(msg, ##__VA_ARGS__); \
        printf("\n"); \
        tests_failed++; \
    } while (0)

/* ========================================================================
 * Test Cases
 * ======================================================================== */

/* Test discovery packet encode/decode round-trip */
static void test_discovery_round_trip(void)
{
    TEST("test_discovery_round_trip");

    pt_discovery_packet pkt_out, pkt_in;
    uint8_t buf[PT_DISCOVERY_MAX_SIZE];
    int ret;

    /* Create test packet */
    pkt_out.version = PT_PROTOCOL_VERSION;
    pkt_out.type = PT_DISC_TYPE_ANNOUNCE;
    pkt_out.flags = PT_DISC_FLAG_HOST | PT_DISC_FLAG_ACCEPTING;
    pkt_out.sender_port = 7354;
    pkt_out.transports = PT_DISC_TRANSPORT_TCP | PT_DISC_TRANSPORT_UDP;
    strcpy(pkt_out.name, "TestPeer");
    pkt_out.name_len = (uint8_t)strlen(pkt_out.name);

    /* Encode */
    ret = pt_discovery_encode(&pkt_out, buf, sizeof(buf));
    if (ret < 0) {
        FAIL("Encode failed: %d", ret);
        return;
    }

    /* Decode */
    ret = pt_discovery_decode(NULL, buf, ret, &pkt_in);
    if (ret != 0) {
        FAIL("Decode failed: %d", ret);
        return;
    }

    /* Verify all fields */
    if (pkt_in.version != pkt_out.version ||
        pkt_in.type != pkt_out.type ||
        pkt_in.flags != pkt_out.flags ||
        pkt_in.sender_port != pkt_out.sender_port ||
        pkt_in.transports != pkt_out.transports ||
        pkt_in.name_len != pkt_out.name_len ||
        strcmp(pkt_in.name, pkt_out.name) != 0) {
        FAIL("Field mismatch");
        return;
    }

    PASS();
}

/* Test message header encode/decode round-trip */
static void test_message_header_round_trip(void)
{
    TEST("test_message_header_round_trip");

    pt_message_header hdr_out, hdr_in;
    uint8_t buf[PT_MESSAGE_HEADER_SIZE];
    int ret;

    /* Create test header */
    hdr_out.version = PT_PROTOCOL_VERSION;
    hdr_out.type = PT_MSG_TYPE_DATA;
    hdr_out.flags = PT_MSG_FLAG_UNRELIABLE | PT_MSG_FLAG_NO_DELAY;
    hdr_out.sequence = 42;
    hdr_out.payload_len = 1234;

    /* Encode */
    ret = pt_message_encode_header(&hdr_out, buf);
    if (ret != PT_MESSAGE_HEADER_SIZE) {
        FAIL("Encode returned %d, expected %d", ret, PT_MESSAGE_HEADER_SIZE);
        return;
    }

    /* Decode */
    ret = pt_message_decode_header(NULL, buf, ret, &hdr_in);
    if (ret != 0) {
        FAIL("Decode failed: %d", ret);
        return;
    }

    /* Verify all fields */
    if (hdr_in.version != hdr_out.version ||
        hdr_in.type != hdr_out.type ||
        hdr_in.flags != hdr_out.flags ||
        hdr_in.sequence != hdr_out.sequence ||
        hdr_in.payload_len != hdr_out.payload_len) {
        FAIL("Field mismatch");
        return;
    }

    PASS();
}

/* Test CRC detects single-bit corruption */
static void test_crc_corruption(void)
{
    TEST("test_crc_corruption");

    pt_discovery_packet pkt;
    uint8_t buf[PT_DISCOVERY_MAX_SIZE];
    int ret, packet_size;

    /* Create and encode packet */
    pkt.version = PT_PROTOCOL_VERSION;
    pkt.type = PT_DISC_TYPE_ANNOUNCE;
    pkt.flags = 0;
    pkt.sender_port = 7354;
    pkt.transports = PT_DISC_TRANSPORT_TCP;
    strcpy(pkt.name, "Test");
    pkt.name_len = 4;

    packet_size = pt_discovery_encode(&pkt, buf, sizeof(buf));
    if (packet_size < 0) {
        FAIL("Encode failed");
        return;
    }

    /* Flip one bit in the name */
    buf[12] ^= 0x01;

    /* Decode should fail with CRC error */
    ret = pt_discovery_decode(NULL, buf, packet_size, &pkt);
    if (ret != PT_ERR_CRC) {
        FAIL("Expected PT_ERR_CRC, got %d", ret);
        return;
    }

    PASS();
}

/* Test invalid magic rejection */
static void test_invalid_magic(void)
{
    TEST("test_invalid_magic");

    pt_discovery_packet pkt;
    uint8_t buf[PT_DISCOVERY_MAX_SIZE];
    int ret;

    /* Create valid packet first */
    pkt.version = PT_PROTOCOL_VERSION;
    pkt.type = PT_DISC_TYPE_ANNOUNCE;
    pkt.flags = 0;
    pkt.sender_port = 7354;
    pkt.transports = PT_DISC_TRANSPORT_TCP;
    strcpy(pkt.name, "Test");
    pkt.name_len = 4;

    ret = pt_discovery_encode(&pkt, buf, sizeof(buf));
    if (ret < 0) {
        FAIL("Encode failed");
        return;
    }

    /* Corrupt magic */
    buf[0] = 'X';

    /* Decode should fail with magic error */
    ret = pt_discovery_decode(NULL, buf, ret, &pkt);
    if (ret != PT_ERR_MAGIC) {
        FAIL("Expected PT_ERR_MAGIC, got %d", ret);
        return;
    }

    PASS();
}

/* Test invalid version rejection */
static void test_invalid_version(void)
{
    TEST("test_invalid_version");

    pt_discovery_packet pkt;
    uint8_t buf[PT_DISCOVERY_MAX_SIZE];
    int ret;

    /* Create packet with wrong version */
    pkt.version = 99;  /* Invalid version */
    pkt.type = PT_DISC_TYPE_ANNOUNCE;
    pkt.flags = 0;
    pkt.sender_port = 7354;
    pkt.transports = PT_DISC_TRANSPORT_TCP;
    strcpy(pkt.name, "Test");
    pkt.name_len = 4;

    ret = pt_discovery_encode(&pkt, buf, sizeof(buf));
    if (ret < 0) {
        FAIL("Encode failed");
        return;
    }

    /* Decode should fail with version error */
    ret = pt_discovery_decode(NULL, buf, ret, &pkt);
    if (ret != PT_ERR_VERSION) {
        FAIL("Expected PT_ERR_VERSION, got %d", ret);
        return;
    }

    PASS();
}

/* Test truncated packet rejection */
static void test_truncated_packet(void)
{
    TEST("test_truncated_packet");

    pt_discovery_packet pkt;
    uint8_t buf[PT_DISCOVERY_MAX_SIZE];
    int ret;

    /* Test 1: Discovery packet too short (only 10 bytes) */
    ret = pt_discovery_decode(NULL, buf, 10, &pkt);
    if (ret != PT_ERR_TRUNCATED) {
        FAIL("Expected PT_ERR_TRUNCATED for short discovery, got %d", ret);
        return;
    }

    /* Test 2: Message header too short (only 5 bytes) */
    pt_message_header hdr;
    ret = pt_message_decode_header(NULL, buf, 5, &hdr);
    if (ret != PT_ERR_TRUNCATED) {
        FAIL("Expected PT_ERR_TRUNCATED for short message, got %d", ret);
        return;
    }

    PASS();
}

/* Test CRC-16 known values */
static void test_crc16_known_values(void)
{
    TEST("test_crc16_known_values");

    const uint8_t test_data[] = "123456789";
    uint16_t crc;

    /* Standard check value for this algorithm */
    crc = pt_crc16(test_data, 9);
    if (crc != 0x2189) {
        FAIL("Expected 0x2189, got 0x%04X", crc);
        return;
    }

    /* Verify pt_crc16_check */
    if (!pt_crc16_check(test_data, 9, 0x2189)) {
        FAIL("pt_crc16_check failed for valid CRC");
        return;
    }

    if (pt_crc16_check(test_data, 9, 0x0000)) {
        FAIL("pt_crc16_check passed for invalid CRC");
        return;
    }

    PASS();
}

/* Test CRC-16 incremental update */
static void test_crc16_update(void)
{
    TEST("test_crc16_update");

    const uint8_t part1[] = "12345";
    const uint8_t part2[] = "6789";
    const uint8_t full[] = "123456789";
    uint16_t crc_full, crc_incremental;

    /* Compute full CRC */
    crc_full = pt_crc16(full, 9);

    /* Compute incremental CRC */
    crc_incremental = pt_crc16(part1, 5);
    crc_incremental = pt_crc16_update(crc_incremental, part2, 4);

    if (crc_full != crc_incremental) {
        FAIL("CRC mismatch: full=0x%04X, incremental=0x%04X",
             crc_full, crc_incremental);
        return;
    }

    PASS();
}

/* Test UDP message encode/decode round-trip */
static void test_udp_round_trip(void)
{
    TEST("test_udp_round_trip");

    const char *payload = "Hello UDP";
    uint16_t payload_len = (uint16_t)strlen(payload);
    uint16_t sender_port = 7355;
    uint8_t buf[256];
    int ret;

    /* Encode */
    ret = pt_udp_encode(payload, payload_len, sender_port, buf, sizeof(buf));
    if (ret < 0) {
        FAIL("Encode failed: %d", ret);
        return;
    }

    /* Decode */
    uint16_t port_out;
    const void *payload_out;
    uint16_t len_out;

    ret = pt_udp_decode(NULL, buf, ret, &port_out, &payload_out, &len_out);
    if (ret != 0) {
        FAIL("Decode failed: %d", ret);
        return;
    }

    /* Verify */
    if (port_out != sender_port ||
        len_out != payload_len ||
        memcmp(payload_out, payload, payload_len) != 0) {
        FAIL("Field mismatch");
        return;
    }

    PASS();
}

/* Test UDP invalid magic and truncation */
static void test_udp_errors(void)
{
    TEST("test_udp_errors");

    uint8_t buf[256];
    uint16_t port_out;
    const void *payload_out;
    uint16_t len_out;
    int ret;

    /* Test 1: Truncated packet (only 5 bytes) */
    ret = pt_udp_decode(NULL, buf, 5, &port_out, &payload_out, &len_out);
    if (ret != PT_ERR_TRUNCATED) {
        FAIL("Expected PT_ERR_TRUNCATED for short packet, got %d", ret);
        return;
    }

    /* Test 2: Invalid magic */
    buf[0] = 'X';
    buf[1] = 'Y';
    buf[2] = 'Z';
    buf[3] = 'W';
    buf[4] = 0;
    buf[5] = 0;
    buf[6] = 0;
    buf[7] = 0;

    ret = pt_udp_decode(NULL, buf, PT_UDP_HEADER_SIZE, &port_out,
                        &payload_out, &len_out);
    if (ret != PT_ERR_MAGIC) {
        FAIL("Expected PT_ERR_MAGIC, got %d", ret);
        return;
    }

    PASS();
}

/* Test pt_strerror error message mapping */
static void test_strerror(void)
{
    TEST("test_strerror");

    /* Test a few known error codes */
    const char *msg;

    msg = PeerTalk_ErrorString(PT_ERR_CRC);
    if (strcmp(msg, "CRC validation failed") != 0) {
        FAIL("Wrong message for PT_ERR_CRC: '%s'", msg);
        return;
    }

    msg = PeerTalk_ErrorString(PT_ERR_MAGIC);
    if (strcmp(msg, "Invalid magic number") != 0) {
        FAIL("Wrong message for PT_ERR_MAGIC: '%s'", msg);
        return;
    }

    msg = PeerTalk_ErrorString(PT_ERR_VERSION);
    if (strcmp(msg, "Protocol version mismatch") != 0) {
        FAIL("Wrong message for PT_ERR_VERSION: '%s'", msg);
        return;
    }

    PASS();
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("=== Protocol Tests ===\n\n");

    /* Run all tests */
    test_discovery_round_trip();
    test_message_header_round_trip();
    test_crc_corruption();
    test_invalid_magic();
    test_invalid_version();
    test_truncated_packet();
    test_crc16_known_values();
    test_crc16_update();
    test_udp_round_trip();
    test_udp_errors();
    test_strerror();

    /* Summary */
    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
