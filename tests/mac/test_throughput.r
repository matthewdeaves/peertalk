/*
 * test_throughput.r - Resources for PT Throughput Test
 *
 * SIZE resource: 3MB preferred, 2MB minimum
 * Throughput testing needs larger buffers for streaming.
 */

data 'SIZE' (-1) {
    /* Flags: 0x5880 = standard MultiFinder-aware settings */
    $"5880"
    /* Preferred size: 3MB = 0x00300000 */
    $"00300000"
    /* Minimum size: 2MB = 0x00200000 */
    $"00200000"
};
