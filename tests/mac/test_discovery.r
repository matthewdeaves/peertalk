/*
 * test_discovery.r - Resources for PT Discovery Test
 *
 * SIZE resource: 2MB preferred, 1MB minimum
 * Discovery testing needs less memory (no TCP connections).
 */

data 'SIZE' (-1) {
    /* Flags: 0x5880 = standard MultiFinder-aware settings */
    $"5880"
    /* Preferred size: 2MB = 0x00200000 */
    $"00200000"
    /* Minimum size: 1MB = 0x00100000 */
    $"00100000"
};
