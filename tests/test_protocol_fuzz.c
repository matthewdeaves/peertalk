/* test_protocol_fuzz.c - Fuzz and stress tests for protocol parsing
 *
 * Tests protocol resilience against:
 * - Malformed magic bytes
 * - Invalid versions
 * - Bad packet types
 * - Truncated packets
 * - Corrupted CRC
 * - Oversized fields
 * - Random garbage data
 * - Boundary conditions
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/core/protocol.h"
#include "../include/peertalk.h"

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

/* Simple PRNG for reproducible tests */
static uint32_t g_seed = 12345;

static uint32_t rand_next(void)
{
    g_seed = g_seed * 1103515245 + 12345;
    return (g_seed >> 16) & 0x7FFF;
}

static void fill_random(uint8_t *buf, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        buf[i] = (uint8_t)(rand_next() & 0xFF);
    }
}

/* ========================================================================
 * Discovery Packet Fuzz Tests
 * ======================================================================== */

static void test_discovery_bad_magic(void)
{
    TEST("test_discovery_bad_magic");

    uint8_t buf[64];
    pt_discovery_packet pkt;
    int err;

    /* Create valid packet first */
    pt_discovery_packet valid = {
        .version = 1,
        .type = PT_DISC_TYPE_ANNOUNCE,
        .flags = 0,
        .sender_port = 7354,
        .transports = PT_DISC_TRANSPORT_TCP,
        .name_len = 4,
        .name = "Test"
    };
    int len = pt_discovery_encode(&valid, buf, sizeof(buf));

    /* Corrupt each magic byte */
    int i;
    for (i = 0; i < 4; i++) {
        uint8_t save = buf[i];
        buf[i] ^= 0xFF;
        err = pt_discovery_decode(NULL, buf, len, &pkt);
        if (err != PT_ERR_MAGIC) {
            FAIL("Expected PT_ERR_MAGIC for corrupted byte %d, got %d", i, err);
            return;
        }
        buf[i] = save;
    }

    PASS();
}

static void test_discovery_bad_version(void)
{
    TEST("test_discovery_bad_version");

    uint8_t buf[64];
    pt_discovery_packet pkt;

    pt_discovery_packet valid = {
        .version = 1,
        .type = PT_DISC_TYPE_ANNOUNCE,
        .flags = 0,
        .sender_port = 7354,
        .transports = PT_DISC_TRANSPORT_TCP,
        .name_len = 4,
        .name = "Test"
    };
    int len = pt_discovery_encode(&valid, buf, sizeof(buf));

    /* Set invalid version */
    buf[4] = 99;

    /* Recalculate CRC */
    uint16_t crc = pt_crc16(buf, len - 2);
    buf[len - 2] = (crc >> 8) & 0xFF;
    buf[len - 1] = crc & 0xFF;

    int err = pt_discovery_decode(NULL, buf, len, &pkt);
    if (err != PT_ERR_VERSION) {
        FAIL("Expected PT_ERR_VERSION, got %d", err);
        return;
    }

    PASS();
}

static void test_discovery_bad_type(void)
{
    TEST("test_discovery_bad_type");

    uint8_t buf[64];
    pt_discovery_packet pkt;

    pt_discovery_packet valid = {
        .version = 1,
        .type = PT_DISC_TYPE_ANNOUNCE,
        .flags = 0,
        .sender_port = 7354,
        .transports = PT_DISC_TRANSPORT_TCP,
        .name_len = 4,
        .name = "Test"
    };
    int len = pt_discovery_encode(&valid, buf, sizeof(buf));

    /* Set invalid type (0x00, 0x04-0xFF are invalid) */
    uint8_t invalid_types[] = {0x00, 0x04, 0x05, 0x10, 0x80, 0xFF};
    size_t i;

    for (i = 0; i < sizeof(invalid_types); i++) {
        buf[5] = invalid_types[i];

        /* Recalculate CRC */
        uint16_t crc = pt_crc16(buf, len - 2);
        buf[len - 2] = (crc >> 8) & 0xFF;
        buf[len - 1] = crc & 0xFF;

        int err = pt_discovery_decode(NULL, buf, len, &pkt);
        if (err != PT_ERR_INVALID_PARAM) {
            FAIL("Expected PT_ERR_INVALID_PARAM for type 0x%02X, got %d",
                 invalid_types[i], err);
            return;
        }
    }

    PASS();
}

static void test_discovery_truncated(void)
{
    TEST("test_discovery_truncated");

    uint8_t buf[64];
    pt_discovery_packet pkt;

    pt_discovery_packet valid = {
        .version = 1,
        .type = PT_DISC_TYPE_ANNOUNCE,
        .flags = 0,
        .sender_port = 7354,
        .transports = PT_DISC_TRANSPORT_TCP,
        .name_len = 4,
        .name = "Test"
    };
    int len = pt_discovery_encode(&valid, buf, sizeof(buf));

    /* Try decoding with progressively shorter lengths */
    int i;
    for (i = 0; i < len - 1; i++) {
        int err = pt_discovery_decode(NULL, buf, i, &pkt);
        if (err != PT_ERR_TRUNCATED) {
            FAIL("Expected PT_ERR_TRUNCATED for len %d, got %d", i, err);
            return;
        }
    }

    PASS();
}

static void test_discovery_bad_crc(void)
{
    TEST("test_discovery_bad_crc");

    uint8_t buf[64];
    pt_discovery_packet pkt;

    pt_discovery_packet valid = {
        .version = 1,
        .type = PT_DISC_TYPE_ANNOUNCE,
        .flags = 0,
        .sender_port = 7354,
        .transports = PT_DISC_TRANSPORT_TCP,
        .name_len = 4,
        .name = "Test"
    };
    int len = pt_discovery_encode(&valid, buf, sizeof(buf));

    /* Corrupt CRC */
    buf[len - 1] ^= 0xFF;

    int err = pt_discovery_decode(NULL, buf, len, &pkt);
    if (err != PT_ERR_CRC) {
        FAIL("Expected PT_ERR_CRC, got %d", err);
        return;
    }

    PASS();
}

static void test_discovery_bad_name_len(void)
{
    TEST("test_discovery_bad_name_len");

    uint8_t buf[64];
    pt_discovery_packet pkt;

    pt_discovery_packet valid = {
        .version = 1,
        .type = PT_DISC_TYPE_ANNOUNCE,
        .flags = 0,
        .sender_port = 7354,
        .transports = PT_DISC_TRANSPORT_TCP,
        .name_len = 4,
        .name = "Test"
    };
    int len = pt_discovery_encode(&valid, buf, sizeof(buf));

    /* Set name_len > 31 */
    buf[11] = 32;  /* name_len field */

    /* Recalculate CRC */
    uint16_t crc = pt_crc16(buf, len - 2);
    buf[len - 2] = (crc >> 8) & 0xFF;
    buf[len - 1] = crc & 0xFF;

    int err = pt_discovery_decode(NULL, buf, len, &pkt);
    if (err != PT_ERR_INVALID_PARAM) {
        FAIL("Expected PT_ERR_INVALID_PARAM for name_len=32, got %d", err);
        return;
    }

    PASS();
}

static void test_discovery_random_garbage(void)
{
    TEST("test_discovery_random_garbage");

    uint8_t buf[128];
    pt_discovery_packet pkt;
    int i;

    /* Feed 1000 random buffers - should never crash */
    for (i = 0; i < 1000; i++) {
        size_t len = (rand_next() % 64) + 1;
        fill_random(buf, len);

        /* This should never crash, just return an error */
        int err = pt_discovery_decode(NULL, buf, len, &pkt);
        (void)err;  /* We don't care what error, just that it doesn't crash */
    }

    /* If we get here without crashing, we pass */
    printf("(1000 random inputs) ");
    PASS();
}

/* ========================================================================
 * Message Header Fuzz Tests
 * ======================================================================== */

static void test_message_bad_magic(void)
{
    TEST("test_message_bad_magic");

    uint8_t buf[16];
    pt_message_header hdr;

    /* Build valid header */
    pt_message_header valid = {
        .version = 1,
        .type = PT_MSG_TYPE_DATA,
        .flags = 0,
        .sequence = 0,
        .payload_len = 100
    };
    pt_message_encode_header(&valid, buf);

    /* Corrupt magic */
    int i;
    for (i = 0; i < 4; i++) {
        uint8_t save = buf[i];
        buf[i] ^= 0xFF;
        int err = pt_message_decode_header(NULL, buf, 10, &hdr);
        if (err != PT_ERR_MAGIC) {
            FAIL("Expected PT_ERR_MAGIC for corrupted byte %d, got %d", i, err);
            return;
        }
        buf[i] = save;
    }

    PASS();
}

static void test_message_bad_type(void)
{
    TEST("test_message_bad_type");

    uint8_t buf[16];
    pt_message_header hdr;

    pt_message_header valid = {
        .version = 1,
        .type = PT_MSG_TYPE_DATA,
        .flags = 0,
        .sequence = 0,
        .payload_len = 100
    };
    pt_message_encode_header(&valid, buf);

    /* Set invalid type (0x07 is PT_MSG_TYPE_CAPABILITY, so it's valid now) */
    uint8_t invalid_types[] = {0x00, 0x08, 0x10, 0x80, 0xFF};
    size_t i;

    for (i = 0; i < sizeof(invalid_types); i++) {
        buf[5] = invalid_types[i];  /* type field */
        int err = pt_message_decode_header(NULL, buf, 10, &hdr);
        if (err != PT_ERR_INVALID_PARAM) {
            FAIL("Expected PT_ERR_INVALID_PARAM for type 0x%02X, got %d",
                 invalid_types[i], err);
            return;
        }
    }

    PASS();
}

static void test_message_truncated(void)
{
    TEST("test_message_truncated");

    uint8_t buf[16];
    pt_message_header hdr;

    pt_message_header valid = {
        .version = 1,
        .type = PT_MSG_TYPE_DATA,
        .flags = 0,
        .sequence = 0,
        .payload_len = 100
    };
    pt_message_encode_header(&valid, buf);

    /* Try with lengths 0-9 (header is 10 bytes) */
    int i;
    for (i = 0; i < 10; i++) {
        int err = pt_message_decode_header(NULL, buf, i, &hdr);
        if (err != PT_ERR_TRUNCATED) {
            FAIL("Expected PT_ERR_TRUNCATED for len %d, got %d", i, err);
            return;
        }
    }

    PASS();
}

static void test_message_random_garbage(void)
{
    TEST("test_message_random_garbage");

    uint8_t buf[64];
    pt_message_header hdr;
    int i;

    for (i = 0; i < 1000; i++) {
        size_t len = (rand_next() % 32) + 1;
        fill_random(buf, len);
        int err = pt_message_decode_header(NULL, buf, len, &hdr);
        (void)err;
    }

    printf("(1000 random inputs) ");
    PASS();
}

/* ========================================================================
 * UDP Message Fuzz Tests
 * ======================================================================== */

static void test_udp_bad_magic(void)
{
    TEST("test_udp_bad_magic");

    uint8_t buf[64];
    uint16_t sender_port;
    const void *payload;
    uint16_t payload_len;

    /* Build valid UDP packet */
    uint8_t data[] = {1, 2, 3, 4};
    int len = pt_udp_encode(data, sizeof(data), 7355, buf, sizeof(buf));

    /* Corrupt magic */
    int i;
    for (i = 0; i < 4; i++) {
        uint8_t save = buf[i];
        buf[i] ^= 0xFF;
        int err = pt_udp_decode(NULL, buf, len, &sender_port, &payload, &payload_len);
        if (err != PT_ERR_MAGIC) {
            FAIL("Expected PT_ERR_MAGIC for corrupted byte %d, got %d", i, err);
            return;
        }
        buf[i] = save;
    }

    PASS();
}

static void test_udp_truncated(void)
{
    TEST("test_udp_truncated");

    uint8_t buf[64];
    uint16_t sender_port;
    const void *payload;
    uint16_t payload_len;

    uint8_t data[] = {1, 2, 3, 4};
    pt_udp_encode(data, sizeof(data), 7355, buf, sizeof(buf));

    /* Try with lengths 0-7 (header is 8 bytes) */
    int i;
    for (i = 0; i < 8; i++) {
        int err = pt_udp_decode(NULL, buf, i, &sender_port, &payload, &payload_len);
        if (err != PT_ERR_TRUNCATED) {
            FAIL("Expected PT_ERR_TRUNCATED for len %d, got %d", i, err);
            return;
        }
    }

    PASS();
}

static void test_udp_random_garbage(void)
{
    TEST("test_udp_random_garbage");

    uint8_t buf[128];
    uint16_t sender_port;
    const void *payload;
    uint16_t payload_len;
    int i;

    for (i = 0; i < 1000; i++) {
        size_t len = (rand_next() % 64) + 1;
        fill_random(buf, len);
        int err = pt_udp_decode(NULL, buf, len, &sender_port, &payload, &payload_len);
        (void)err;
    }

    printf("(1000 random inputs) ");
    PASS();
}

/* ========================================================================
 * Protocol Encode/Decode Round-Trip Tests
 * ======================================================================== */

static void test_discovery_encode_decode_roundtrip(void)
{
    TEST("test_discovery_encode_decode_roundtrip");

    pt_discovery_packet pkt, decoded;
    uint8_t buf[64];
    int i;

    /* Test various name lengths */
    int sizes[] = {0, 1, 8, 15, 16, 30, 31};
    size_t num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    for (i = 0; i < (int)num_sizes; i++) {
        int size = sizes[i];

        pkt.version = 1;
        pkt.type = PT_DISC_TYPE_ANNOUNCE;
        pkt.flags = (uint16_t)(rand_next() & 0xFFFF);
        pkt.sender_port = 7354;
        pkt.transports = 0x03;
        pkt.name_len = (uint8_t)size;
        memset(pkt.name, 'A' + (i % 26), size);
        pkt.name[size] = '\0';

        int len = pt_discovery_encode(&pkt, buf, sizeof(buf));
        if (len < 0) {
            FAIL("Encode failed for name size %d: %d", size, len);
            return;
        }

        int err = pt_discovery_decode(NULL, buf, len, &decoded);
        if (err != 0) {
            FAIL("Decode failed for name size %d: %d", size, err);
            return;
        }

        if (decoded.name_len != (uint8_t)size) {
            FAIL("Name length mismatch for size %d: got %d", size, decoded.name_len);
            return;
        }

        if (decoded.flags != pkt.flags) {
            FAIL("Flags mismatch for size %d: expected 0x%04X, got 0x%04X",
                 size, pkt.flags, decoded.flags);
            return;
        }
    }

    PASS();
}

static void test_discovery_bit_flip_detection(void)
{
    TEST("test_discovery_bit_flip_detection");

    pt_discovery_packet pkt, decoded;
    uint8_t buf[64];
    int flips_detected = 0;
    int flips_missed = 0;
    int i, bit;

    pkt.version = 1;
    pkt.type = PT_DISC_TYPE_ANNOUNCE;
    pkt.flags = 0x1234;
    pkt.sender_port = 7354;
    pkt.transports = 0x03;
    pkt.name_len = 8;
    memcpy(pkt.name, "TestPeer", 9);

    int len = pt_discovery_encode(&pkt, buf, sizeof(buf));

    /* Flip each bit and verify CRC catches it */
    for (i = 0; i < len; i++) {
        for (bit = 0; bit < 8; bit++) {
            uint8_t save = buf[i];
            buf[i] ^= (1 << bit);

            int err = pt_discovery_decode(NULL, buf, len, &decoded);
            if (err != 0) {
                flips_detected++;
            } else {
                /* Decode succeeded - check if data changed */
                if (decoded.flags != pkt.flags ||
                    decoded.sender_port != pkt.sender_port ||
                    decoded.name_len != pkt.name_len) {
                    flips_missed++;
                }
            }

            buf[i] = save;
        }
    }

    printf("\n    Bit flips: %d detected, %d missed (%.2f%% detection)\n",
           flips_detected, flips_missed,
           100.0 * flips_detected / (flips_detected + flips_missed + 1));

    /* CRC-16 should catch nearly all single-bit errors */
    if (flips_missed > 0) {
        FAIL("%d bit flips not detected by CRC", flips_missed);
    } else {
        PASS();
    }
}

/* ========================================================================
 * Boundary Condition Tests
 * ======================================================================== */

static void test_discovery_boundary_names(void)
{
    TEST("test_discovery_boundary_names");

    uint8_t buf[64];
    pt_discovery_packet pkt, decoded;
    int err;

    /* Empty name (len=0) */
    pkt.version = 1;
    pkt.type = PT_DISC_TYPE_ANNOUNCE;
    pkt.flags = 0;
    pkt.sender_port = 7354;
    pkt.transports = PT_DISC_TRANSPORT_TCP;
    pkt.name_len = 0;
    pkt.name[0] = '\0';

    int len = pt_discovery_encode(&pkt, buf, sizeof(buf));
    if (len < 0) {
        FAIL("Failed to encode empty name: %d", len);
        return;
    }

    err = pt_discovery_decode(NULL, buf, len, &decoded);
    if (err != 0) {
        FAIL("Failed to decode empty name: %d", err);
        return;
    }

    /* Maximum name (len=31) */
    pkt.name_len = 31;
    memset(pkt.name, 'A', 31);
    pkt.name[31] = '\0';

    len = pt_discovery_encode(&pkt, buf, sizeof(buf));
    if (len < 0) {
        FAIL("Failed to encode max-length name: %d", len);
        return;
    }

    err = pt_discovery_decode(NULL, buf, len, &decoded);
    if (err != 0) {
        FAIL("Failed to decode max-length name: %d", err);
        return;
    }

    if (decoded.name_len != 31) {
        FAIL("Name length mismatch: expected 31, got %d", decoded.name_len);
        return;
    }

    PASS();
}

static void test_message_boundary_payloads(void)
{
    TEST("test_message_boundary_payloads");

    pt_message_header hdr;
    uint8_t buf[16];

    /* Zero payload */
    hdr.version = 1;
    hdr.type = PT_MSG_TYPE_DATA;
    hdr.flags = 0;
    hdr.sequence = 0;
    hdr.payload_len = 0;

    pt_message_encode_header(&hdr, buf);
    pt_message_header decoded;
    int err = pt_message_decode_header(NULL, buf, 10, &decoded);
    if (err != 0 || decoded.payload_len != 0) {
        FAIL("Failed for zero payload: err=%d, len=%d", err, decoded.payload_len);
        return;
    }

    /* Maximum payload (65535) */
    hdr.payload_len = 65535;
    pt_message_encode_header(&hdr, buf);
    err = pt_message_decode_header(NULL, buf, 10, &decoded);
    if (err != 0 || decoded.payload_len != 65535) {
        FAIL("Failed for max payload: err=%d, len=%d", err, decoded.payload_len);
        return;
    }

    PASS();
}

static void test_crc_edge_cases(void)
{
    TEST("test_crc_edge_cases");

    /* Empty input */
    uint16_t crc = pt_crc16(NULL, 0);
    /* CRC of nothing should be initial value (0) */

    /* Single byte */
    uint8_t one = 0x00;
    crc = pt_crc16(&one, 1);

    /* All zeros */
    uint8_t zeros[64] = {0};
    crc = pt_crc16(zeros, sizeof(zeros));

    /* All ones */
    uint8_t ones[64];
    memset(ones, 0xFF, sizeof(ones));
    crc = pt_crc16(ones, sizeof(ones));

    /* Known test vector: "123456789" */
    const char *test_str = "123456789";
    crc = pt_crc16((const uint8_t *)test_str, 9);
    if (crc != 0x2189) {
        FAIL("CRC test vector failed: expected 0x2189, got 0x%04X", crc);
        return;
    }

    /* Incremental CRC should match single-shot */
    uint16_t crc1 = pt_crc16((const uint8_t *)test_str, 9);
    uint16_t crc2 = pt_crc16((const uint8_t *)test_str, 4);
    crc2 = pt_crc16_update(crc2, (const uint8_t *)test_str + 4, 5);
    if (crc1 != crc2) {
        FAIL("Incremental CRC mismatch: 0x%04X vs 0x%04X", crc1, crc2);
        return;
    }

    PASS();
}

/* ========================================================================
 * Stress Tests
 * ======================================================================== */

static void test_stress_encode_decode(void)
{
    TEST("test_stress_encode_decode");

    pt_discovery_packet pkt, decoded;
    uint8_t buf[64];
    int i;
    int success = 0, fail = 0;

    for (i = 0; i < 10000; i++) {
        int name_len = rand_next() % 32;

        pkt.version = 1;
        pkt.type = (uint8_t)(PT_DISC_TYPE_ANNOUNCE + (rand_next() % 3));
        pkt.flags = (uint16_t)(rand_next() & 0xFFFF);
        pkt.sender_port = (uint16_t)(rand_next() & 0xFFFF);
        pkt.transports = (uint8_t)(rand_next() & 0x07);
        pkt.name_len = (uint8_t)name_len;
        fill_random((uint8_t *)pkt.name, name_len);
        pkt.name[name_len] = '\0';

        int len = pt_discovery_encode(&pkt, buf, sizeof(buf));
        if (len < 0) {
            fail++;
            continue;
        }

        int err = pt_discovery_decode(NULL, buf, len, &decoded);
        if (err != 0) {
            fail++;
            continue;
        }

        if (decoded.name_len != pkt.name_len ||
            decoded.flags != pkt.flags ||
            decoded.sender_port != pkt.sender_port) {
            fail++;
            continue;
        }

        success++;
    }

    printf("\n    10000 round-trips: %d success, %d fail\n", success, fail);

    if (fail > 0) {
        FAIL("%d round-trips failed", fail);
    } else {
        PASS();
    }
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("=== Protocol Fuzz Tests ===\n\n");

    /* Seed PRNG for reproducibility */
    g_seed = 12345;

    printf("Discovery Packet Tests:\n");
    test_discovery_bad_magic();
    test_discovery_bad_version();
    test_discovery_bad_type();
    test_discovery_truncated();
    test_discovery_bad_crc();
    test_discovery_bad_name_len();
    test_discovery_random_garbage();

    printf("\nMessage Header Tests:\n");
    test_message_bad_magic();
    test_message_bad_type();
    test_message_truncated();
    test_message_random_garbage();

    printf("\nUDP Message Tests:\n");
    test_udp_bad_magic();
    test_udp_truncated();
    test_udp_random_garbage();

    printf("\nRound-Trip Tests:\n");
    test_discovery_encode_decode_roundtrip();
    test_discovery_bit_flip_detection();

    printf("\nBoundary Tests:\n");
    test_discovery_boundary_names();
    test_message_boundary_payloads();
    test_crc_edge_cases();

    printf("\nStress Tests:\n");
    test_stress_encode_decode();

    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
