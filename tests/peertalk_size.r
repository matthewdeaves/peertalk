/*
 * peertalk_size.r - SIZE resource for PeerTalk test applications
 *
 * Overrides the default Retro68 SIZE resource (1MB/1MB) with larger
 * heap sizes needed for network buffers, MacTCP/OT state, and clog.
 *
 * Without this, MaxApplZone() cannot extend the heap beyond 1MB,
 * and PT_Init's buffer allocations fail or leave no room for the
 * networking stack. The v1 PeerTalk test apps used 2.5-3MB.
 *
 * SIZE resource format:
 *   2 bytes flags + 4 bytes preferred size + 4 bytes minimum size
 *
 * Flags 0x5880:
 *   bit 14: acceptSuspendResumeEvents
 *   bit 12: canBackground
 *   bit 11: doesActivateOnFGSwitch
 *   bit 7:  is32BitCompatible
 */

data 'SIZE' (-1) {
    $"5880"
    $"00280000"  /* Preferred: 2.5 MB */
    $"00180000"  /* Minimum:   1.5 MB */
};
