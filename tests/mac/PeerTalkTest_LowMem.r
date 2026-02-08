/*
 * PeerTalk Test Application Resources - Low Memory Version
 *
 * For Mac SE and other low-memory machines (4MB or less).
 * Uses smaller heap to fit in available memory.
 */

data 'SIZE' (-1) {
    /* Flags: 0x5880 = standard MultiFinder-aware settings */
    $"5880"
    /* Preferred size: 1MB = 0x00100000 */
    $"00100000"
    /* Minimum size: 512KB = 0x00080000 */
    $"00080000"
};
