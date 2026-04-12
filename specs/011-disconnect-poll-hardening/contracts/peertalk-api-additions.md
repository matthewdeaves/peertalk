# API Contract: PT_DisconnectAll

**Extends**: `specs/001-peertalk-sdk/contracts/peertalk-api.md`

## New Function

### PT_DisconnectAll

```c
void PT_DisconnectAll(PT_Context *ctx);
```

**Purpose**: Disconnect all currently connected peers with goodbye frames. Convenience function for lifecycle transitions (game→lobby, rematch, reconnect).

**Parameters**:
- `ctx`: Valid PT_Context pointer from PT_Init. NULL is a no-op.

**Returns**: void (matches PT_Disconnect convention — disconnect is best-effort)

**Behavior**:
1. Iterates all peer slots in the context's peer array
2. For each peer where `in_use == 1` and `state == PT_PEER_CONNECTED`:
   a. Sends goodbye frame (4-byte TCP: `[0x00 0x00 0xFF 0x00]`)
   b. Calls platform tcp_disconnect to close the connection
   c. Sets peer state to PT_PEER_DISCONNECTED
   d. Fires on_disconnected callback with reason PT_QUIT
3. Peers in DISCOVERED or DISCONNECTED state are not affected
4. Discovery state is not affected (remains active or stopped)

**Preconditions**:
- Must be called from main event loop (not from interrupt/ASR context)
- ctx must be a valid initialized context (or NULL for no-op)

**Postconditions**:
- All previously CONNECTED peers are now DISCONNECTED
- Remote peers have received goodbye frames (best-effort)
- on_disconnected callback has fired once per disconnected peer
- Peer slots remain allocated (in_use = 1) — peers are still discoverable

**Edge cases**:
- No connected peers: No-op, no callbacks fired
- NULL ctx: No-op
- Goodbye send fails (broken TCP): Local disconnect proceeds, remote detects via RST/timeout
- Called from on_disconnected callback: Safe — remaining peers still iterated correctly

**Thread safety**: None required (cooperative single-threaded on all platforms)

## Header Placement

Added to `include/peertalk.h` in the "Connections (2)" section, which becomes "Connections (3)":

```c
/* ------------------------------------------------------------------ */
/* Connections (3)                                                     */
/* ------------------------------------------------------------------ */

PT_Status PT_Connect(PT_Context *ctx, PT_Peer *peer);
void      PT_Disconnect(PT_Context *ctx, PT_Peer *peer);
void      PT_DisconnectAll(PT_Context *ctx);
```

## No Changes to Existing Functions

All 22 existing public API functions are unchanged. This adds function #23.
