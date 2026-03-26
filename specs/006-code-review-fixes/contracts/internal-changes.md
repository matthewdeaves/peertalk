# Internal Behavior Changes: Code Review Fixes

**No public API changes.** All 22 functions in peertalk.h retain their existing signatures and semantics, with one behavioral clarification:

## PT_Broadcast Behavior Change

**Before**: `PT_Broadcast` returns `PT_ERR_SEND_FAILED` when no peers are connected.
**After**: `PT_Broadcast` returns `PT_OK` when no peers are connected (no-op). Returns `PT_ERR_SEND_FAILED` only when a send to a connected peer actually fails.

This is a semantic clarification, not a signature change. Callers that checked for `PT_ERR_SEND_FAILED` to detect "no peers" should use `PT_GetPeerCount` instead.

## Internal Changes (not visible to callers)

| Change | Files | Impact |
|--------|-------|--------|
| Atomic flag exchange (MacTCP) | pt_mactcp.c | Interrupt-disable around flag snapshot; eliminates theoretical flag loss race |
| Atomic flag exchange (OT) | pt_ot.c | OTAtomic* bit operations replace non-atomic RMW; flags type changes from unsigned long to UInt8 |
| Init failure cleanup (MacTCP) | pt_mactcp.c | Resources freed on init failure instead of leaked |
| Init failure cleanup (OT) | pt_ot.c | Endpoints closed and UPPs disposed on init failure |
| Reassembly admission fix | pt_messaging.c | Per-chunk bounds check replaces overestimating aggregate check |
| POSIX UDP drain loop | pt_posix.c | All pending datagrams read per poll instead of one |

## Wire Protocol

No changes. Discovery, TCP framing, UDP framing, and chunking formats are unchanged.
