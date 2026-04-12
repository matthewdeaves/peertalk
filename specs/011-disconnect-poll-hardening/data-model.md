# Data Model: 011-disconnect-poll-hardening

**Date**: 2026-04-12

## Entities

### PT_Context (existing — no changes)

The context owns the peer array and platform operations. PT_DisconnectAll receives the context and iterates its peer array.

- `peers[]`: Fixed-size array of PT_Peer_Internal, allocated at init
- `max_peers`: Array capacity, determined by available RAM at init
- `callbacks`: Struct of registered callback function pointers
- `platform_ops`: Vtable of platform-specific operations

### PT_Peer (existing — no changes)

Each peer slot has an `in_use` flag and a state field.

- `in_use`: 0 = slot free, 1 = slot allocated
- `state`: PT_PEER_DISCOVERED | PT_PEER_CONNECTED | PT_PEER_DISCONNECTED
- `platform_peer.endpoint`: OT endpoint slot pointer (OT platform)
- `platform_peer.tcp_stream`: MacTCP stream pointer (MacTCP platform)
- `platform_peer.tcp_fd`: Socket fd (POSIX platform)

### State Transitions

```
PT_DisconnectAll affects only CONNECTED → DISCONNECTED transitions:

  DISCOVERED ──PT_Connect()──→ CONNECTED ──PT_Disconnect()──→ DISCONNECTED
                                    │                              ▲
                                    └──PT_DisconnectAll()───────────┘
                                    
  (DISCOVERED and DISCONNECTED peers are not touched by PT_DisconnectAll)
```

## No New Entities

This feature adds no new data structures. PT_DisconnectAll operates on existing peer array. Poll hardening adds error checking to existing code paths without new state.
