/*
 * PeerTalk Compact Header Tests
 *
 * Tests for the compact 4-byte header format that reduces protocol
 * overhead from 12 bytes to 4 bytes for small messages.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../include/peertalk.h"
#include "../src/core/protocol.h"

/* Test utilities */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  %s... ", #name)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg, ...) do { printf("FAIL: " msg "\n", ##__VA_ARGS__); tests_failed++; } while(0)
#define ASSERT(cond) do { if (!(cond)) { FAIL("assertion failed: %s", #cond); return; } } while(0)

/* ========================================================================
 * Compact Header Encoding Tests
 * ======================================================================== */

static void test_encode_compact_data(void) {
    TEST(test_encode_compact_data);

    pt_compact_header hdr;
    uint8_t buf[4];
    int len;

    hdr.type = PT_MSG_TYPE_DATA;
    hdr.flags = 0;
    hdr.payload_len = 256;

    len = pt_message_encode_compact(&hdr, buf);

    ASSERT(len == PT_COMPACT_HEADER_SIZE);
    ASSERT(buf[0] == PT_COMPACT_MARKER);  /* 'P' */
    ASSERT(buf[1] == 0x10);  /* type=1 in high nibble, flags=0 in low nibble */
    ASSERT(buf[2] == 0x01);  /* high byte of 256 */
    ASSERT(buf[3] == 0x00);  /* low byte of 256 */

    PASS();
}

static void test_encode_compact_with_flags(void) {
    TEST(test_encode_compact_with_flags);

    pt_compact_header hdr;
    uint8_t buf[4];

    hdr.type = PT_MSG_TYPE_DATA;
    hdr.flags = 0x05;  /* Example flags that fit in 4 bits */
    hdr.payload_len = 100;

    pt_message_encode_compact(&hdr, buf);

    /* TypeFlags = (type << 4) | flags = (1 << 4) | 5 = 0x15 */
    ASSERT(buf[1] == 0x15);

    PASS();
}

static void test_encode_compact_types(void) {
    TEST(test_encode_compact_types);

    pt_compact_header hdr;
    uint8_t buf[4];

    /* Test various message types */
    uint8_t types[] = {
        PT_MSG_TYPE_DATA,
        PT_MSG_TYPE_PING,
        PT_MSG_TYPE_PONG,
        PT_MSG_TYPE_DISCONNECT,
        PT_MSG_TYPE_CAPABILITY
    };

    for (int i = 0; i < (int)(sizeof(types) / sizeof(types[0])); i++) {
        hdr.type = types[i];
        hdr.flags = 0;
        hdr.payload_len = 0;

        pt_message_encode_compact(&hdr, buf);

        /* Type should be in high nibble */
        uint8_t encoded_type = (buf[1] >> 4) & 0x0F;
        ASSERT(encoded_type == types[i]);
    }

    PASS();
}

/* ========================================================================
 * Compact Header Decoding Tests
 * ======================================================================== */

static void test_decode_compact_basic(void) {
    TEST(test_decode_compact_basic);

    uint8_t buf[4] = { 'P', 0x10, 0x01, 0x00 };  /* DATA, flags=0, len=256 */
    pt_compact_header hdr;
    int result;

    result = pt_message_decode_compact(buf, sizeof(buf), &hdr);

    ASSERT(result == 0);
    ASSERT(hdr.type == PT_MSG_TYPE_DATA);
    ASSERT(hdr.flags == 0);
    ASSERT(hdr.payload_len == 256);

    PASS();
}

static void test_decode_compact_with_flags(void) {
    TEST(test_decode_compact_with_flags);

    uint8_t buf[4] = { 'P', 0x15, 0x00, 0x64 };  /* DATA, flags=5, len=100 */
    pt_compact_header hdr;

    pt_message_decode_compact(buf, sizeof(buf), &hdr);

    ASSERT(hdr.type == PT_MSG_TYPE_DATA);
    ASSERT(hdr.flags == 0x05);
    ASSERT(hdr.payload_len == 100);

    PASS();
}

static void test_decode_compact_invalid_marker(void) {
    TEST(test_decode_compact_invalid_marker);

    uint8_t buf[4] = { 'X', 0x10, 0x00, 0x00 };  /* Invalid marker */
    pt_compact_header hdr;
    int result;

    result = pt_message_decode_compact(buf, sizeof(buf), &hdr);

    ASSERT(result == PT_ERR_MAGIC);

    PASS();
}

static void test_decode_compact_truncated(void) {
    TEST(test_decode_compact_truncated);

    uint8_t buf[3] = { 'P', 0x10, 0x00 };  /* Only 3 bytes */
    pt_compact_header hdr;
    int result;

    result = pt_message_decode_compact(buf, sizeof(buf), &hdr);

    ASSERT(result == PT_ERR_TRUNCATED);

    PASS();
}

/* ========================================================================
 * Format Detection Tests
 * ======================================================================== */

static void test_is_compact_valid(void) {
    TEST(test_is_compact_valid);

    /* Valid compact header: starts with 'P', second byte != 'T' */
    uint8_t compact_buf[4] = { 'P', 0x10, 0x00, 0x00 };

    ASSERT(pt_message_is_compact(compact_buf, 4) == 1);

    PASS();
}

static void test_is_compact_full_header(void) {
    TEST(test_is_compact_full_header);

    /* Full header magic: "PTMG" */
    uint8_t full_buf[4] = { 'P', 'T', 'M', 'G' };

    ASSERT(pt_message_is_compact(full_buf, 4) == 0);

    PASS();
}

static void test_is_compact_short_buffer(void) {
    TEST(test_is_compact_short_buffer);

    uint8_t buf[1] = { 'P' };

    /* Not enough bytes to determine format */
    ASSERT(pt_message_is_compact(buf, 1) == 0);

    PASS();
}

static void test_is_compact_not_p(void) {
    TEST(test_is_compact_not_p);

    uint8_t buf[4] = { 'X', 0x10, 0x00, 0x00 };

    /* First byte is not 'P' */
    ASSERT(pt_message_is_compact(buf, 4) == 0);

    PASS();
}

/* ========================================================================
 * Roundtrip Tests
 * ======================================================================== */

static void test_roundtrip_data_message(void) {
    TEST(test_roundtrip_data_message);

    pt_compact_header original, decoded;
    uint8_t buf[4];

    original.type = PT_MSG_TYPE_DATA;
    original.flags = 0x03;
    original.payload_len = 1024;

    pt_message_encode_compact(&original, buf);
    pt_message_decode_compact(buf, sizeof(buf), &decoded);

    ASSERT(decoded.type == original.type);
    ASSERT(decoded.flags == original.flags);
    ASSERT(decoded.payload_len == original.payload_len);

    PASS();
}

static void test_roundtrip_ping_message(void) {
    TEST(test_roundtrip_ping_message);

    pt_compact_header original, decoded;
    uint8_t buf[4];

    original.type = PT_MSG_TYPE_PING;
    original.flags = 0;
    original.payload_len = 0;

    pt_message_encode_compact(&original, buf);
    pt_message_decode_compact(buf, sizeof(buf), &decoded);

    ASSERT(decoded.type == original.type);
    ASSERT(decoded.flags == original.flags);
    ASSERT(decoded.payload_len == original.payload_len);

    PASS();
}

/* ========================================================================
 * Edge Cases
 * ======================================================================== */

static void test_max_payload_length(void) {
    TEST(test_max_payload_length);

    pt_compact_header hdr;
    uint8_t buf[4];

    /* Max 16-bit payload length */
    hdr.type = PT_MSG_TYPE_DATA;
    hdr.flags = 0;
    hdr.payload_len = 0xFFFF;

    pt_message_encode_compact(&hdr, buf);

    pt_compact_header decoded;
    pt_message_decode_compact(buf, sizeof(buf), &decoded);

    ASSERT(decoded.payload_len == 0xFFFF);

    PASS();
}

static void test_all_flag_bits(void) {
    TEST(test_all_flag_bits);

    pt_compact_header hdr;
    uint8_t buf[4];

    /* Test all 4-bit flag combinations */
    for (int flags = 0; flags <= 0x0F; flags++) {
        hdr.type = PT_MSG_TYPE_DATA;
        hdr.flags = (uint8_t)flags;
        hdr.payload_len = 0;

        pt_message_encode_compact(&hdr, buf);

        pt_compact_header decoded;
        pt_message_decode_compact(buf, sizeof(buf), &decoded);

        if (decoded.flags != (uint8_t)flags) {
            FAIL("flags mismatch: expected 0x%02X, got 0x%02X", flags, decoded.flags);
            return;
        }
    }

    PASS();
}

static void test_fragment_flag_doesnt_fit(void) {
    TEST(test_fragment_flag_doesnt_fit);

    /* PT_MSG_FLAG_FRAGMENT = 0x10 doesn't fit in 4-bit flags field */
    /* This test documents the limitation */

    pt_compact_header hdr;
    uint8_t buf[4];

    hdr.type = PT_MSG_TYPE_DATA;
    hdr.flags = PT_MSG_FLAG_FRAGMENT;  /* 0x10 */
    hdr.payload_len = 256;

    pt_message_encode_compact(&hdr, buf);

    pt_compact_header decoded;
    pt_message_decode_compact(buf, sizeof(buf), &decoded);

    /* Fragment flag gets truncated to 0 in 4-bit field */
    ASSERT(decoded.flags == 0);  /* (0x10 & 0x0F) == 0 */

    /* This is why fragments use full headers, not compact headers */

    PASS();
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void) {
    printf("Compact Header Tests\n");
    printf("====================\n\n");

    printf("Encoding:\n");
    test_encode_compact_data();
    test_encode_compact_with_flags();
    test_encode_compact_types();

    printf("\nDecoding:\n");
    test_decode_compact_basic();
    test_decode_compact_with_flags();
    test_decode_compact_invalid_marker();
    test_decode_compact_truncated();

    printf("\nFormat Detection:\n");
    test_is_compact_valid();
    test_is_compact_full_header();
    test_is_compact_short_buffer();
    test_is_compact_not_p();

    printf("\nRoundtrip:\n");
    test_roundtrip_data_message();
    test_roundtrip_ping_message();

    printf("\nEdge Cases:\n");
    test_max_payload_length();
    test_all_flag_bits();
    test_fragment_flag_doesnt_fit();

    printf("\n====================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
