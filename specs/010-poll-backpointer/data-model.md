# Data Model: Poll Back-Pointer Optimisation

**Feature**: 010-poll-backpointer  
**Date**: 2026-04-06

## Modified Entities

### TCPStreamSlot (MacTCP — pt_mactcp.c)

Existing per-stream state struct. Gains one field.

| Field | Type | Description | Modified? |
|-------|------|-------------|-----------|
| stream | StreamPtr | MacTCP stream reference | No |
| send_pb | TCPiopb | Parameter block for sends | No |
| open_pb | TCPiopb | Parameter block for open/close | No |
| buffer | Ptr | TCP receive buffer (8192 bytes) | No |
| wds[3] | wdsEntry | Scatter-gather for sends | No |
| flags | volatile unsigned char | ASR-set event flags | No |
| state | int | STREAM_FREE/LISTENING/CONNECTING/CONNECTED | No |
| send_pending | int | Async send in progress | No |
| **owner** | **PT_Peer_Internal \*** | **Back-pointer to owning peer, NULL if free** | **NEW** |

**Lifecycle**:
- Initialised to NULL when stream is created in `mactcp_init()`
- Set to peer pointer when stream is assigned (outgoing connect or incoming accept)
- Cleared to NULL in `abort_stream()` and `mactcp_tcp_disconnect()`
- Rejection path: if `pt_handle_incoming_connection()` rejects an incoming connection (no room), the stream goes through `abort_stream()` which clears owner — so owner is never left stale after rejection

### OTEndpointSlot (OT — pt_ot.c)

Existing per-endpoint state struct. Gains one field.

| Field | Type | Description | Modified? |
|-------|------|-------------|-----------|
| ep | EndpointRef | OT endpoint reference | No |
| flags | volatile UInt8 | Notifier-set event flags | No |
| state | int | EP_FREE/LISTENING/CONNECTING/CONNECTED | No |
| flow_off | volatile int | Flow control active | No |
| **owner** | **PT_Peer_Internal \*** | **Back-pointer to owning peer, NULL if free** | **NEW** |

**Lifecycle**:
- Initialised to NULL when endpoint is created in `ot_init()`
- Set to peer pointer when endpoint is assigned (outgoing connect or incoming accept)
- Cleared to NULL in `ot_tcp_disconnect()`, the reject-no-room reset path in `ot_poll()`, and the failed active connect reset path in `ot_poll()`
- Rejection path: if `pt_handle_incoming_connection()` rejects an incoming connection (no room), the endpoint goes through the unbind/rebind reset which clears owner — so owner is never left stale after rejection

## Removed Entities

### find_peer_for_stream (MacTCP)

Static function removed. Was: linear scan of `ctx->peers[]` comparing `platform_peer.tcp_stream` to a `TCPStreamSlot *`. All 4 call sites replaced with `ts->owner`.

### find_peer_for_ep (OT)

Static function removed. Was: linear scan of `ctx->peers[]` comparing `platform_peer.endpoint` to an `OTEndpointSlot *`. All 5 call sites replaced with `slot->owner`.

## Unchanged Entities

- **PT_Peer_Internal**: Not modified. Already contains `platform_peer.tcp_stream` (MacTCP) / `platform_peer.endpoint` (OT) as the forward pointer.
- **PT_Context_Internal**: Not modified.
- **PT_PlatformPeer**: Not modified.
- **PosixState**: Not modified. POSIX backend does not use reverse lookups.
