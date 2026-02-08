/*
 * PeerTalk Test Application Resources
 *
 * Custom SIZE resource for test applications that need more heap
 * for TCP buffers and throughput testing.
 *
 * SIZE resource format: 16 flags (2 bytes) + preferred (4 bytes) + minimum (4 bytes)
 * Using raw hex to avoid include path issues.
 */

data 'SIZE' (-1) {
    /* Flags: 0x5880 = standard MultiFinder-aware settings */
    $"5880"
    /* Preferred size: 2MB = 0x00200000 */
    $"00200000"
    /* Minimum size: 1MB = 0x00100000 */
    $"00100000"
};
