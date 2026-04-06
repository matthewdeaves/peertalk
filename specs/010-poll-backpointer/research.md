# Research: Poll Back-Pointer Optimisation

**Feature**: 010-poll-backpointer  
**Date**: 2026-04-06

## R1: Back-Pointer Safety in Interrupt/Notifier Context

**Decision**: The back-pointer (`PT_Peer_Internal *owner`) is only read and written from the main loop (poll functions), never from ASR callbacks or OT notifiers. No interrupt-safety concerns.

**Rationale**: MacTCP ASR callbacks (`tcp_asr`) only set volatile flags — they never access `TCPStreamSlot.owner`. OT notifiers (`tcp_notifier`) only call `OTAtomicSetBit` on the flags field — they never access `OTEndpointSlot.owner`. The back-pointer is set during connection setup (main loop) and cleared during disconnect (main loop). Per Inside Macintosh Volume V (Lines 58110-58116), completion routines must not access unlocked handles — but the back-pointer is a `Ptr` in a locked block (NewPtrClear), so this is not applicable regardless.

**Alternatives considered**: Making the pointer volatile was considered but rejected — it is never accessed at interrupt time, so volatile would add unnecessary load/store overhead on 68k.

## R2: Stale Pointer Prevention

**Decision**: Clear the back-pointer to NULL in every code path that releases a stream/endpoint slot: `abort_stream()`, `mactcp_tcp_disconnect()`, `ot_tcp_disconnect()`, and the reject-no-room paths in poll. The pointer is set only when a peer takes ownership of a slot.

**Rationale**: A stale pointer (pointing to a freed or reassigned peer) would cause silent data corruption or bus errors on 68k. The safest approach is to always clear on release, rather than relying on callers to remember. Since `abort_stream()` already resets `flags` and `state`, adding `owner = NULL` there catches all MacTCP release paths. For OT, the synchronous unbind/rebind reset sequence in `ot_tcp_disconnect()` is the single cleanup point.

**Alternatives considered**: Using an index into the peer array instead of a pointer (would survive reallocation). Rejected because peer arrays are never reallocated after init — the pointer is stable for the lifetime of the context.

## R3: Assignment Points for Back-Pointer

**Decision**: Set the back-pointer at the exact points where `platform_peer.tcp_stream` (MacTCP) or `platform_peer.endpoint` (OT) is assigned.

**Rationale**: The back-pointer must be the inverse of the forward pointer. When `peer->platform_peer.tcp_stream = ts`, we must also set `ts->owner = peer`. This guarantees the bidirectional link is consistent. The assignment points are:

**MacTCP (`pt_mactcp.c`)**:
- `mactcp_tcp_connect()`: outgoing connection — `peer->platform_peer.tcp_stream = ts` → add `ts->owner = peer`
- `mactcp_poll()` passive open completion: incoming connection — after `pt_handle_incoming_connection()` confirms peer accepted, set `ts->owner = peer`

**OT (`pt_ot.c`)**:
- `ot_tcp_connect()`: outgoing connection — `peer->platform_peer.endpoint = slot` → add `slot->owner = peer`
- `ot_poll()` listener T_LISTEN handling: incoming connection — after `pt_handle_incoming_connection()` confirms peer accepted, set `slot->owner = peer`

**Alternatives considered**: Setting the pointer inside `pt_handle_incoming_connection()` in core code. Rejected because that function doesn't know the platform slot type — the assignment must happen in platform code.

## R4: Impact on find_peer_for_stream / find_peer_for_ep Call Sites

**Decision**: Replace all call sites with direct `ts->owner` / `slot->owner` dereference, then remove both functions.

**Rationale**: Call site audit:

**MacTCP `find_peer_for_stream` call sites (4)**:
1. Passive open completion — find peer to verify acceptance → use `ts->owner`
2. Data available (FLAG_DATA_AVAIL) — find peer to read into recv buffer → use `ts->owner`
3. Remote close/terminate — find peer to drain and disconnect → use `ts->owner`
4. Remote close/terminate (second call for drain-parse) — same block, use `ts->owner`

**OT `find_peer_for_ep` call sites (5)**:
1. Active connect completion (T_CONNECT) — find peer to mark connected → use `slot->owner`
2. Disconnect event (T_DISCONNECT) — find peer to drain and disconnect → use `slot->owner`
3. Orderly release (T_ORDREL) — find peer to drain and disconnect → use `slot->owner`
4. Data available (T_DATA) — find peer to read into recv buffer → use `slot->owner`
5. Listener no-room check — find peer to verify acceptance → use `slot->owner`

All call sites follow the same pattern: `peer = find_peer_for_X(ctx, slot); if (peer) { ... }`. The replacement is `peer = slot->owner; if (peer) { ... }`.

**Alternatives considered**: Keeping the scan functions as debug-mode assertions. Rejected — adds complexity for no production benefit. The test apps exercise all paths.

## R5: Memory Impact

**Decision**: Acceptable. 4 bytes per slot on 68k/PPC.

**Rationale**: `TCPStreamSlot` gains one `PT_Peer_Internal *` (4 bytes on 68k, 4 bytes on PPC). With MAX_TCP_STREAMS=32 slots, total is 128 bytes. `OTEndpointSlot` gains the same — 128 bytes. Total memory increase: 256 bytes across both platforms. This is within the `PT_PEER_METADATA` padding already allocated per peer (128 bytes per peer). The platform state structs (`MacTCPState`, `OTState`) are global statics, not part of the pre-allocated memory block, so this doesn't affect the FreeMem() budget.

**Alternatives considered**: None — 256 bytes is negligible on all target platforms (4 MB minimum).
