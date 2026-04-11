<!--
Sync Impact Report
- Version change: 1.1.0 → 1.1.1
- Modified principles:
  - II: Added "connection liveness" to the list of invisible protocol concerns,
    and explicit mention of keepalives so apps never need heartbeat logic.
  - VI: Clarified that fixed-interval protocol behaviors (discovery broadcasts,
    TCP keepalives) are not "runtime adaptation" — they run unconditionally on
    a timer and require no decision-making.
- Templates requiring updates: None (PATCH — clarifications only)
- Follow-up TODOs: None
-->

# PeerTalk Constitution

## Project Overview

### What PeerTalk Is

A C networking SDK for peer-to-peer LAN communication between
modern computers (Linux, macOS) and Classic Macintosh (MacTCP
on 68k, Open Transport on PPC).

### What It's For

Three applications define the scope. Every feature MUST serve
at least one:

1. **Bomberman clone** — real-time position updates, small
   frequent messages, 2-6 players
2. **Chess game** — turn-based moves, tiny infrequent messages,
   2 players
3. **Chat application** — text messages, variable size, any
   number of peers (connected peer count scales with available
   RAM)

If a feature does not help build one of these three, it does
not belong in the SDK.

### What the Developer Sees

```c
PT_Init(&ctx, "PlayerOne");
PT_OnPeerDiscovered(ctx, on_peer_found, NULL);
PT_OnPeerLost(ctx, on_peer_lost, NULL);
PT_OnConnected(ctx, on_connected, NULL);
PT_OnDisconnected(ctx, on_disconnected, NULL);
PT_OnError(ctx, on_error, NULL);

PT_RegisterMessage(ctx, MSG_POSITION, PT_FAST);
PT_RegisterMessage(ctx, MSG_CHAT, PT_RELIABLE);
PT_OnMessage(ctx, MSG_POSITION, on_position, NULL);
PT_OnMessage(ctx, MSG_CHAT, on_chat, NULL);

PT_StartDiscovery(ctx);
PT_Connect(ctx, peer);
PT_Send(ctx, peer, MSG_POSITION, &pos, sizeof(pos));
PT_Broadcast(ctx, MSG_POSITION, &pos, sizeof(pos));
PT_Poll(ctx);
PT_StopDiscovery(ctx);

int count = PT_GetPeerCount(ctx);
PT_Peer *peer = PT_GetPeer(ctx, i);
const char *name = PT_PeerName(peer);
PT_PeerState state = PT_GetPeerState(peer);

PT_Shutdown(ctx);
```

The developer declares intent once per message type — fast
(UDP) or reliable (TCP) — then calls `PT_Send`. The SDK picks
the right transport. Unregistered types default to reliable.

## Core Principles

### I. The Three Apps Are the Spec

Every feature MUST serve at least one of the three target
applications (Bomberman, Chess, Chat). No adaptive throttling,
no priority queues, no capability negotiation, no
multi-transport peer merging. If none of the three apps need
it, it does not ship.

### II. The SDK Handles the Protocol

Message framing, type dispatch, transport selection, chunking,
peer discovery, connection lifecycle, and connection liveness
MUST be invisible to the application. The SDK sends keepalives
on idle TCP connections so applications never need their own
heartbeat logic. The developer calls `PT_Send` with any size
payload. The SDK chunks large messages internally and
reassembles on the receiving side — the callback always
delivers the complete message. No explicit size parameter in
the API — the practical limit is the receiver's reassembly
buffer, which scales with available RAM (Principle III).

### III. Honest About Platform Limits

All platform performance characteristics MUST be measured on
real hardware and documented honestly. Never hide a limitation,
but also never assume one exists without measurement. Document
what you measure, not what you expect.

### IV. Simple Defaults, No Knobs

One TCP connection plus one UDP socket per peer. Sensible
buffer sizes. No config structs with 30 fields. If an app
needs tuning, add a setter — never expose internals.

### V. Pre-Allocate Everything

Classic Macs have 4-8 MB RAM and no modern allocator. All
buffers MUST be allocated at init. Zero malloc in the
send/receive path.

### VI. Adapt at Init, Not at Runtime

The SDK MUST check available memory at startup and size
buffers, peer slots, and receive windows accordingly. A 4 MB
Mac SE gets smaller buffers and fewer peer slots. A 48 MB
Performa 6400 gets more headroom. This happens automatically
— the developer calls `PT_Init()` and the SDK does the right
thing. There is no runtime adaptation, no capability
negotiation, no dynamic tuning. Fixed-interval protocol
behaviors (discovery broadcasts, TCP keepalives) are not
"runtime adaptation" — they run unconditionally on a timer and
require no decision-making. The wire protocol and API are
identical on every platform.

### VII. Logging Is a Separate Library

PeerTalk depends on clog for logging. clog is developed in its
own repository (`~/Desktop/clog`) and linked as a static
library. Applications MAY use clog independently.

### VIII. Test Apps and Demo Apps Prove the SDK

The SDK ships two kinds of proof:

1. **Test apps** — small, automated programs that exercise SDK
   patterns: frequent small sends (Bomberman-like),
   request/response (Chess-like), variable-size text
   (Chat-like). If the test apps work on a Mac SE and a Linux
   box, the SDK works.

2. **Demo apps** — flagship applications that rewrite an
   existing app on top of the SDK. The first demo app is
   csend-pt (PeerTalk Chat), which reuses the proven csend GUI
   but replaces all networking with PeerTalk SDK calls. Demo
   apps ship as examples of what the SDK enables — they are
   real apps, not tests.

### IX. Keep It Small

Target: under 15,000 lines total across all platforms. Three
platform backends need room, but if the codebase grows past
15K lines, something is wrong.

### X. C89 for Portability

All SDK code MUST compile as C89/C90 for Classic Mac
compatibility. POSIX-only code paths (test apps, build tools)
MAY use C11.

## Scope & Deliverables

### Platforms

| Platform | System | Role |
|----------|--------|------|
| POSIX | Linux, macOS | Reference implementation, CI testing |
| MacTCP | System 6-7.5, 68k | Primary Classic Mac target |
| Open Transport | System 7.6+, PPC/late 68k | Secondary Classic Mac target |

AppleTalk is out of scope.

### What Ships

- `peertalk.h` — single public header
- Static libraries per platform
- Test applications (POSIX + Classic Mac)
- Demo applications (csend-pt: PeerTalk Chat)
- Documentation with platform performance characteristics

### What Does Not Ship

- Priority queues, capability negotiation
- Multi-transport anything
- Adaptive tuning, rate limiting, coalesce hash tables
- Config structs with more than 5 fields

## Definition of Done

PeerTalk is done when:

- A test app on Linux can discover and exchange messages with
  a test app on a Mac SE (MacTCP) and a Performa 6400 (OT)
- The API fits on one screen
- The code is simple enough to be fun to read

## Governance

This constitution is the highest-authority document for
PeerTalk development decisions. All implementation work MUST
comply with these principles.

- **Amendments** require documenting the change rationale,
  updating this file, and propagating changes to dependent
  templates (plan, spec, tasks).
- **Versioning** follows semantic versioning:
  - MAJOR: Principle removals or backward-incompatible
    redefinitions
  - MINOR: New principles added or existing guidance
    materially expanded
  - PATCH: Clarifications, wording fixes, non-semantic
    refinements
- **Compliance review**: Every feature plan MUST include a
  Constitution Check gate verifying alignment with these
  principles before implementation begins.
- **Scope disputes**: When uncertain whether a feature belongs,
  apply Principle I — if none of the three target apps need it,
  it does not ship.

**Version**: 1.1.1 | **Ratified**: 2026-02-28 | **Last Amended**: 2026-04-10
