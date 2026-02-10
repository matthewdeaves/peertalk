/*
 * test_mactcp.r - Resources for PT MacTCP Test
 *
 * SIZE resource: 2.5MB preferred, 1.5MB minimum
 * Basic MacTCP test needs moderate memory.
 */

data 'SIZE' (-1) {
    /* Flags: 0x5880 = standard MultiFinder-aware settings */
    $"5880"
    /* Preferred size: 2.5MB = 0x00280000 */
    $"00280000"
    /* Minimum size: 1.5MB = 0x00180000 */
    $"00180000"
};
