/*
 * PeerTalk Test Application Resources - Low Memory Version
 *
 * For Macs with 4MB RAM or less (Mac SE, Plus, Classic, etc.)
 * Uses smaller heap to leave room for System and MacTCP.
 *
 * SIZE resource format: 16 flags (2 bytes) + preferred (4 bytes) + minimum (4 bytes)
 */

data 'SIZE' (-1) {
    /* Flags: 0x5880 = standard MultiFinder-aware settings */
    $"5880"
    /* Preferred size: 768KB = 0x000C0000 */
    $"000C0000"
    /* Minimum size: 512KB = 0x00080000 */
    $"00080000"
};
