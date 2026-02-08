/*
 * PeerTalk Test Application Resources - Low Memory Version
 *
 * For Mac SE, Plus, Classic, and other low-memory machines (4MB or less).
 * Uses smaller heap to fit in available memory alongside System 6/7.
 *
 * CRITICAL: Mac SE with 4MB RAM needs this version!
 * The standard 2-3MB heap won't fit.
 */

data 'SIZE' (-1) {
    /* Flags: 0x5880 = standard MultiFinder-aware settings */
    $"5880"
    /* Preferred size: 512KB = 0x00080000 */
    $"00080000"
    /* Minimum size: 384KB = 0x00060000 */
    $"00060000"
};
