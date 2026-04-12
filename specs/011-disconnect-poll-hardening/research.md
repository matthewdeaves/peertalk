# Research: 011-disconnect-poll-hardening

**Date**: 2026-04-12

## R1: PT_DisconnectAll Iteration Safety

**Decision**: Iterate by physical array index (0..max_peers), check `in_use && state == PT_PEER_CONNECTED` per slot, call `send_goodbye()` then `pt_handle_peer_disconnect()` for each. This matches the PT_Shutdown pattern at pt_core.c:385-390 but adds goodbye frames and fires callbacks.

**Rationale**: The peer array uses `in_use` flags and never compacts. Disconnecting a peer changes its state to `PT_PEER_DISCONNECTED` but does not clear `in_use` or shift indices. Iterating by physical index is safe even when state changes mid-iteration. The PT_Shutdown loop already proves this pattern works.

**Alternatives considered**:
- Iterate by logical index via PT_GetPeer(): Fragile because PT_GetPeerCount changes as peers disconnect, and logical-to-physical mapping shifts. Rejected.
- Copy peer pointers to temp array, then disconnect: Unnecessary allocation, violates Principle V. Rejected.

## R2: Goodbye Frame Safety During DisconnectAll

**Decision**: Send goodbye frame before each disconnect, but do not fail the overall operation if send_goodbye fails. The `send_goodbye()` function (pt_core.c:270-279) calls `tcp_send()` which may fail silently if TCP is already broken. The subsequent `pt_handle_peer_disconnect()` proceeds regardless.

**Rationale**: PT_Shutdown explicitly skips goodbye frames because synchronous TCPSend on MacTCP can hang for 60 seconds if the peer is dead. However, PT_DisconnectAll is called during normal operation (game→lobby transition) when peers are presumed alive. Sending goodbye is the right behavior — it matches PT_Disconnect semantics. If a goodbye send fails, the subsequent tcp_disconnect (TCPAbort/OTSndDisconnect) sends RST which the remote side detects.

**Alternatives considered**:
- Skip goodbye like PT_Shutdown does: Would silently drop connections without notifying remote peers. Remote peers would only detect via TCP timeout (60s). Rejected — PT_DisconnectAll is a clean lifecycle transition, not emergency teardown.

## R3: OT OTRcv Error Handling in Disconnect/Ordrel Drain

**Decision**: After each `OTRcv()` call in the disconnect drain (pt_ot.c:894), ordrel drain (pt_ot.c:929), and data receive (pt_ot.c:966) paths, check for negative return values. Treat any negative return as "no data available" — do not process, do not hang. Log at CLOG_DEBUG level.

**Rationale**: OTRcv returns `OTResult` which is negative on error. Common errors on transitional endpoints: `kOTLookErr` (-3158, endpoint needs attention), `kOTOutStateErr` (-3155, wrong state), `kOTNotFoundErr`. The current code only checks `if (nread > 0)` which already skips negative values for the data processing, but doesn't log the error. The risk is not a crash (negative values are already skipped) but silent failures that mask bugs. Adding logging makes disconnect diagnostics easier without changing control flow.

**Alternatives considered**:
- Full error code dispatch (switch on kOTLookErr, kOTOutStateErr, etc.): Over-engineering for the drain paths — we're about to disconnect anyway. Rejected.
- Treat kOTLookErr as fatal and force-disconnect: Unnecessary — the disconnect/ordrel handler already calls pt_handle_peer_disconnect afterward. Rejected.

## R4: OT Endpoint State Validation After Orderly Release

**Decision**: After `OTRcvOrderlyDisconnect()` and `OTSndOrderlyDisconnect()` at pt_ot.c:942-943, re-check `slot->owner` before using the `peer` pointer at line 945. The drain at lines 922-939 calls `pt_messaging_process_tcp_data()` which may process a goodbye frame and trigger `pt_handle_peer_disconnect()`, clearing `slot->owner`.

**Rationale**: If a goodbye frame is found in the drain buffer, `pt_messaging_process_tcp_data()` → goodbye handler → `pt_handle_peer_disconnect()` sets `peer->state = PT_PEER_DISCONNECTED` and calls `ot_tcp_disconnect()` which sets `slot->owner = NULL`. The existing check at line 945 `if (peer && peer->state == PT_PEER_CONNECTED)` already guards against the state change, but uses a potentially stale `peer` pointer from line 922. Re-reading `slot->owner` after the drain ensures we use a valid pointer.

**Alternatives considered**:
- Trust the existing guard: The `peer` pointer itself remains valid (peer slot is never freed, only state changes), so the stale pointer is safe to dereference. The guard `peer->state == PT_PEER_CONNECTED` catches the case. However, re-reading `slot->owner` is defensive and costs one pointer read. Accepted as belt-and-suspenders.

## R5: MacTCP TCPRcv Error Handling in Terminated Drain

**Decision**: At pt_mactcp.c:962, the `PBControlSync()` call already checks `== noErr`. This is correct — non-noErr means TCPRcv failed (stream terminated, connection reset, etc.) and the data is not processed. No change needed to the error check itself. Add a CLOG_DEBUG log when `PBControlSync` returns non-noErr to aid disconnect diagnostics.

**Rationale**: Unlike OT where `OTRcv()` returns a signed result that could be misinterpreted, MacTCP's `PBControlSync()` returns an OSErr and the existing `== noErr` check is the standard pattern. The `rcvBuffLen` field is only meaningful on success. The code is already correct for error handling — the improvement is observability.

**Alternatives considered**:
- Add specific checks for `connectionTerminated`, `connectionDoesntExist`, `invalidStreamPtr`: These are all `!= noErr` and already handled by the existing check. Specific codes could be logged but don't change behavior. Accepted as a logging enhancement only.

## R6: MacTCP Stream State Validation Before TCPRcv

**Decision**: Before issuing TCPRcv in the FLAG_REMOTE_CLOSE/FLAG_TERMINATED drain path (pt_mactcp.c:955), validate that `ts->stream` is still non-NULL and `ts->state` is not `STREAM_FREE`. If the stream was already freed (e.g., by abort_stream from an earlier code path in the same poll iteration), skip the drain.

**Rationale**: On Classic Mac, all code runs cooperatively in the main loop, so there is no true race between poll and abort_stream. However, defensive validation costs one pointer check and protects against future refactoring that might introduce new code paths. The MacTCP `recv_pb` is a shared pooled param block — issuing TCPRcv with a NULL or freed stream handle would cause a MacTCP driver error.

**Alternatives considered**:
- Trust cooperative scheduling and skip the check: Safe today, fragile tomorrow. Rejected — one pointer check is free.
