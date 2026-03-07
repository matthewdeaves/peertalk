# Research: SDK Stability Improvements

**Branch**: `003-sdk-stability` | **Date**: 2026-03-07

## R1: TCP Timeout Values

**Decision**: Increase PT_TCP_TIMEOUT from 30s to 60s, PT_CONNECT_TIMEOUT from 10s to 15s, PT_DISCOVERY_TIMEOUT from 10s to 15s.

**Rationale**: Real-world testing with csend on Classic Mac hardware showed peers disconnecting and reconnecting in 30-second cycles. The 68000 CPU at 8MHz and MacTCP's async completion model can introduce significant latency. 60s inactivity timeout gives ample headroom while still detecting genuinely dead peers within a reasonable window. Discovery timeout increase to 15s matches the longer connection window.

**Alternatives considered**:
- 90s timeout: Too long to detect genuinely dead peers.
- Configurable timeout: Rejected per Constitution IV (No Knobs).
- Keepalive messages: Would add wire protocol complexity. The discovery broadcast every 2s already serves as implicit keepalive for the discovery layer. TCP inactivity timeout is sufficient.

**Files affected**:
- `src/core/pt_internal.h`: Lines 43-45 (three #define constants)
- `src/platform/mactcp/pt_mactcp.c`: Lines 563-565 (ulpTimeoutValue, commandTimeoutValue)

## R2: Duplicate Connection Prevention

**Decision**: IP-based tiebreaker — the peer with the numerically lower IP address (network byte order) is the initiator. When an incoming connection arrives from a peer we're already connecting to, compare IPs. If we should not be the initiator (our IP is higher), close our outgoing attempt and accept the incoming. If we should be the initiator (our IP is lower), reject the incoming and let our outgoing complete.

**Rationale**: IP comparison is deterministic, requires no additional wire protocol messages, and works identically on all platforms. The local IP (`ctx->local_ip`) is already available in network byte order on all three backends.

**Implementation location**: `pt_handle_incoming_connection()` in `src/core/pt_core.c` (line 170). This is the single entry point for all incoming connections across all platforms. Add checks:
1. If peer is already CONNECTED → reject incoming (close the new fd/stream)
2. If peer has `connect_start > 0` (outgoing in progress) → apply tiebreaker

**Edge case — same IP (loopback)**: If `ctx->local_ip == peer_ip`, allow the first connection (no tiebreaker needed since only one side can initiate on loopback).

**Alternatives considered**:
- Random tiebreaker: Non-deterministic, could still result in both sides making the same choice.
- Name-based tiebreaker: Names can be identical ("Unnamed" is the default).
- Sequence number in discovery: Would require wire protocol change.

## R3: Error Callback Peer Context

**Decision**: Change `PT_ErrorCallback` signature from `(PT_Status, const char*, void*)` to `(PT_Peer*, PT_Status, const char*, void*)`. Peer pointer is first parameter, NULL when not peer-specific.

**Rationale**: Peer-first matches the convention of other callbacks (on_connected, on_disconnected, on_message all have peer as first parameter). Having peer context allows apps to display which peer experienced the error.

**Breaking change impact**: All 6 test apps and any external consumers (csend) must update their error callback signature. Since the SDK is pre-1.0 and there's one known external consumer, this is acceptable.

**Internal change**: `pt_fire_error()` gains a `PT_Peer_Internal *peer` parameter. All 10 call sites updated — most already have peer context available. Those that don't (init failures, no-room errors) pass NULL.

**Files affected**:
- `include/peertalk.h`: PT_ErrorCallback typedef
- `src/core/pt_internal.h`: pt_fire_error declaration
- `src/core/pt_core.c`: pt_fire_error implementation + call sites
- `src/core/pt_discovery.c`: call site
- `src/core/pt_messaging.c`: call site
- `src/platform/posix/pt_posix.c`: call site
- `src/platform/mactcp/pt_mactcp.c`: call site
- `src/platform/opentransport/pt_ot.c`: call sites
- All test apps: on_error callback signature

## R4: Shutdown Callback Ordering

**Decision**: Clear all callback pointers at the top of PT_Shutdown() before the disconnect loop.

**Rationale**: Simple, no new API needed. After callbacks are cleared, the goodbye-send and tcp_disconnect calls in the shutdown loop cannot trigger any app callbacks. This is safe because:
1. PT_Shutdown is called from the main loop (not from a callback)
2. After PT_Shutdown, the context is freed — no further PT_Poll calls
3. Apps that need to know about shutdown-time disconnects can track that themselves before calling PT_Shutdown

**Alternative considered**:
- PT_UnregisterCallbacks() API: Unnecessary — clearing at shutdown start achieves the same thing with no new API surface.
- Fire callbacks with a special "shutting down" flag: Adds complexity, apps would still need to guard.

**Files affected**:
- `src/core/pt_core.c`: PT_Shutdown function (line 337)
