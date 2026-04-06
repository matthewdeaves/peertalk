# Research: AppleTalk Transport for PeerTalk

## Overview

Adding AppleTalk as a native transport so PeerTalk clients can communicate
over AppleTalk networks (LocalTalk, EtherTalk) without TCP/IP. Not a bridge
between AppleTalk and TCP/IP peers — a standalone platform backend like
MacTCP or Open Transport TCP/IP.

## Protocol Mapping

PeerTalk's architecture assumes TCP (reliable stream) + UDP (unreliable
datagram) + broadcast discovery. AppleTalk has direct equivalents:

| PeerTalk Concept | Current (TCP/IP) | AppleTalk Equivalent |
|---|---|---|
| Reliable stream | TCP (port 7354) | ADSP (AppleTalk Data Stream Protocol) |
| Unreliable datagram | UDP (ports 7353, 7355) | DDP (Datagram Delivery Protocol) |
| Peer discovery | UDP broadcast + PTLK packet | NBP (Name Binding Protocol) |
| Peer addressing | IP address (dotted quad) | Network:Node:Socket tuple |
| Connection setup | TCP 3-way handshake | ADSP connection open |

### ADSP (reliable stream)

- Full-duplex, flow-controlled byte stream — semantically identical to TCP
- Connection-oriented with open/close handshake
- Built-in attention messages (out-of-band, 570 bytes max)
- Classic API: `.DSP` driver via PBControl (dspInit, dspOpen, dspRead, dspWrite, dspClose)
- OT API: `OTOpenEndpoint` with `"tilisten,adt"` configuration, same notifier pattern as TCP

### DDP (unreliable datagram)

- Best-effort delivery, no ordering guarantees — semantically identical to UDP
- Max packet size: 586 bytes (long DDP header) — PeerTalk's UDP max is 1400 bytes, would need to reduce to ~580
- Socket numbers 1-127 are static (well-known), 128-254 are dynamic
- Broadcast: send to node address 0xFF on the local network
- Classic API: `.MPP` driver
- OT API: `"dgram,ddp"` endpoint

### NBP (discovery)

- Name-based peer lookup built into the AppleTalk stack
- Names are `object:type@zone` tuples (e.g., `"PlayerOne:PeerTalk@*"`)
- `NBPRegister` — advertise this peer's name on the network
- `NBPLookup` — find all peers of a given type (periodic poll, like current discovery timer)
- `NBPRemove` — deregister on shutdown
- Returns AppleTalk addresses directly — no custom wire format needed
- Much cleaner than UDP broadcast for discovery

## Platform Ops Mapping

Current `PT_PlatformOps` vtable (10 functions) and how each maps:

```
init()           -> Open .MPP/.DSP drivers, get node address, allocate ADSP/DDP resources
shutdown()       -> NBPRemove, close all ADSP connections, release DDP sockets
udp_broadcast()  -> DDP send to node 0xFF (discovery), or NBPRegister + NBPLookup
udp_send()       -> DDP send to specific AppleTalk address
udp_listen()     -> DDP socket listener (DDPOpenSocket)
tcp_listen()     -> ADSP passive open (dspCLListen)
tcp_connect()    -> ADSP active open (dspOpen with ocRequest)
tcp_send()       -> ADSP write (dspWrite)
tcp_disconnect() -> ADSP close (dspClose/dspRemove)
poll()           -> Check async completion flags (same pattern as MacTCP)
```

## Discovery: Two Options

### Option A: NBP-Native Discovery (recommended)

Use NBP as the discovery mechanism directly:

1. `PT_StartDiscovery()` calls `NBPRegister("PeerName:PeerTalk@*")` and starts periodic `NBPLookup("=:PeerTalk@*")`
2. Each lookup returns a list of (name, address) tuples
3. Platform feeds new peers into `pt_discovery_receive()` as if they arrived via UDP
4. Peers not seen in subsequent lookups get aged out normally
5. `PT_StopDiscovery()` calls `NBPRemove`

**Pros:** No custom wire protocol for discovery. Names are handled by the OS. Zone-aware.
**Cons:** Requires abstracting discovery slightly — core currently builds/parses PTLK packets directly.

### Option B: DDP Broadcast Discovery

Send the same PTLK discovery packets over DDP broadcast:

1. Allocate a DDP socket for discovery
2. Broadcast the same 37-byte PTLK packet to node 0xFF
3. Receive and parse in `pt_discovery_receive()` — identical to current UDP path

**Pros:** Zero changes to core discovery code.
**Cons:** Ignores NBP (reinvents the wheel). Doesn't work across zones. Wastes a protocol feature.

### Recommendation

Option A (NBP-native). The core change is small: `pt_discovery_receive()` already takes an IP + name
from the platform layer. For AppleTalk, the platform layer would call the same function but with an
AppleTalk address encoded as a string. The discovery timer in `pt_core.c` already drives periodic
activity — the platform just does `NBPLookup` instead of `udp_broadcast`.

## Addressing Impact

### Current: `PT_PeerAddress()` returns dotted-quad IP string

AppleTalk addresses are `network:node:socket` tuples (e.g., `"0:45:128"`). Options:

1. **Format-agnostic string** — return `"0.45.128"` or `"AT:0:45:128"`. Document that format varies by platform.
2. **Add `PT_PeerAddressType()`** — return an enum (PT_ADDR_IP, PT_ADDR_APPLETALK). Adds a knob.
3. **Keep as-is** — apps that display addresses just show whatever string they get.

Recommendation: Option 1. Per constitution IV (no knobs), just return a string. The IP tiebreaker
logic in `pt_handle_incoming_connection()` compares IP addresses numerically — for AppleTalk, compare
`(network << 8 | node)` instead. Same concept, different encoding.

## DDP Payload Size Constraint

DDP max payload is 586 bytes. Current PeerTalk UDP fast messages allow up to 1400 bytes.

Impact: `PT_Send()` with `PT_FAST` transport is limited to ~580 bytes on AppleTalk. This is fine for
the target apps:
- **Bomberman:** Move commands are tiny (<32 bytes)
- **Chess:** Move notation is tiny (<16 bytes)
- **Chat:** Messages could exceed 580 bytes — would need to either fail or chunk

Options:
- Fail with `PT_ERR_SEND_FAILED` if payload > platform max (simple, honest)
- Auto-chunk fast messages (complexity, defeats purpose of "fast")
- Document the limit, let apps handle it

Recommendation: Fail with error. Per constitution III (honest about platform limits).

## Implementation Paths

### Path 1: Classic AppleTalk Manager (68k)

New file: `src/platform/appletalk/pt_appletalk.c`

- Uses `.MPP` driver for DDP, `.DSP` driver for ADSP
- Async parameter blocks with completion routines (same pattern as MacTCP)
- ASR/completion callbacks set volatile flags, process in poll() (ISR safety rules apply)
- Works on any Mac with AppleTalk — which is all of them, back to 1984
- Register preservation: A0, A1, D0, D1, D2 modifiable; D3-D7, A2-A6 must be preserved (same as ADSP completion routines, per Programming With AppleTalk)

Estimated size: ~600-800 lines (similar to pt_mactcp.c at ~750 lines)

Link libraries: none beyond what's already linked (AppleTalk Manager is in ROM/System)

### Path 2: Open Transport AppleTalk (PPC)

Could be a compile-time variant of existing `src/platform/opentransport/pt_ot.c`:

- OT already abstracts the transport family — change endpoint config strings:
  - TCP listener: `"tilisten,tcp"` -> `"tilisten,adt"` (ADSP)
  - TCP endpoint: `"tcp"` -> `"adt"` (ADSP)
  - UDP: `"udp"` -> `"dgram,ddp"` (DDP)
- `InetAddress` -> `DDPAddress` for address structures
- `OTInetStringToAddress` -> manual `DDPAddress` construction
- Notifier pattern, event handling, async flags — all identical
- NBP: `OTRegisterName`, `OTLookupName`, `OTDeleteName`

Estimated delta: ~200-300 lines changed/added vs current pt_ot.c

This path is attractive because it reuses most of the OT infrastructure.

### Path 3: Both

Classic AppleTalk for 68k Macs (Mac SE, Plus, Classic, etc.) and OT AppleTalk for PPC Macs.
This matches the current pattern (MacTCP for 68k, OT for PPC).

## Per-Peer Platform State

```c
/* Classic AppleTalk Manager */
typedef struct PT_PlatformPeer {
    short           adsp_ref;       /* ADSP connection end ref */
    unsigned char   remote_socket;  /* DDP socket for fast msgs */
    AddrBlock       remote_addr;    /* network:node:socket */
    int             dummy;
} PT_PlatformPeer;

/* OT AppleTalk */
typedef struct PT_PlatformPeer {
    void           *endpoint;       /* EndpointRef for ADSP */
    unsigned long   events;         /* volatile flags */
    int             dummy;
} PT_PlatformPeer;
```

The OT variant is identical to the current OT TCP peer state — only the address type changes.

## Build System

New platform flag: `PT_PLATFORM=ATALK` (classic) or `PT_PLATFORM=OT_ATALK` (OT variant)

```bash
# 68k Classic AppleTalk
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/m68k-apple-macos/cmake/retro68.toolchain.cmake \
  -DPT_PLATFORM=ATALK -DCLOG_DIR=$CLOG_DIR -DCLOG_LIB_DIR=$CLOG_DIR/build-m68k

# PPC OT AppleTalk
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
  -DPT_PLATFORM=OT_ATALK -DCLOG_DIR=$CLOG_DIR -DCLOG_LIB_DIR=$CLOG_DIR/build-ppc
```

## Core Changes Required

### Minimal changes to shared code

1. **pt_discovery.c** — Abstract the discovery broadcast/receive path so the platform can use NBP
   instead of raw UDP packets. The simplest approach: add an optional `discover` function pointer
   to `PT_PlatformOps` that, if non-NULL, is called instead of `udp_broadcast` during discovery.
   NBP results feed into `pt_discovery_receive()` with address encoded as string.

2. **pt_internal.h** — Add `#elif defined(PT_PLATFORM_ATALK)` and `#elif defined(PT_PLATFORM_OT_ATALK)`
   to `PT_PlatformPeer` union. Add AppleTalk address field to `PT_Peer_Internal` (or reuse the
   existing IP field with a different encoding).

3. **pt_core.c** — The IP tiebreaker in `pt_handle_incoming_connection()` uses `strcmp()` on IP
   strings. Would work as-is if AppleTalk addresses are formatted consistently (e.g., zero-padded).

4. **peertalk.h** — No changes needed. The 24-function public API is transport-agnostic.

### Wire protocol

Unchanged. ADSP is a byte stream (like TCP), so the 4/8-byte frame headers work identically.
DDP is a datagram (like UDP), so the 3-byte fast message headers work identically.

## Testing

### Hardware available

All Macs have AppleTalk built in:
- **Mac SE** (68000) — LocalTalk port, could also do EtherTalk via Asante adapter
- **Performa 630** (68040) — Ethernet (EtherTalk)
- **Performa 6200** (PPC 603) — Ethernet (EtherTalk)
- **Performa 6400** (PPC 603e) — Ethernet (EtherTalk)

Any two machines on the same EtherTalk segment can test. The Mac SE could test
LocalTalk-to-LocalTalk with another LocalTalk Mac, or EtherTalk with the others.

### Test plan

The existing test apps (test_lifecycle, test_fast, test_reliable) should work
unmodified — they use the public PT_* API which is transport-agnostic. Just
rebuild with `PT_PLATFORM=ATALK` and run on two Macs.

## Risks and Open Questions

1. **ADSP connection limits** — How many simultaneous ADSP connections can a Mac SE handle?
   MacTCP streams are capped at 32 in PeerTalk. Need to check ADSP limits.

2. **NBP lookup latency** — How fast does NBPLookup return? If it blocks for seconds, it
   could stall PT_Poll. Need to verify async NBP is available on all targets.

3. **EtherTalk vs LocalTalk** — Transparent to the application? Should be, but worth verifying
   that ADSP/DDP/NBP work identically over both physical layers.

4. **Zone routing** — NBP can search `@*` (all zones) but DDP broadcast is zone-local. If peers
   are in different zones, discovery works but fast messages might not reach. Probably not
   relevant for our LAN setup but worth documenting.

5. **Retro68 AppleTalk support** — Do the Retro68 import libraries include AppleTalk Manager
   symbols? The OT libs needed custom stubs (`ot_slm_stubs`). May need similar for AppleTalk.

6. **Simultaneous TCP/IP + AppleTalk** — Not in scope (this is AppleTalk-only builds), but
   worth noting that a future dual-stack build is theoretically possible since the platform
   ops vtable could be swapped or multiplexed.

## Reference Material

- `books/Programming_With_AppleTalk_1991.txt` — Primary reference for classic AppleTalk Manager API
- `books/NetworkingOpenTransport.txt` — OT AppleTalk endpoint configuration
- `books/Inside_Macintosh_Volume_VI_1991.txt` — Table B-3 (interrupt-safe routines)
- `books/Inside_Macintosh_Volume_V_1986.txt` — Completion routine restrictions

## Effort Summary

| Component | Estimated LOC | Complexity |
|---|---|---|
| Classic AppleTalk backend (`pt_appletalk.c`) | 600-800 | Medium (similar to MacTCP) |
| OT AppleTalk variant (delta to `pt_ot.c`) | 200-300 | Low (endpoint config swap) |
| Core discovery abstraction | 50-100 | Low |
| Core addressing changes | 20-40 | Trivial |
| CMake additions | 30-50 | Trivial |
| **Total new/changed** | **~900-1300** | Stays under 15K LOC budget |

The OT path is the quickest win. The classic AppleTalk path covers more hardware but is
a larger standalone effort. Doing both matches the existing MacTCP/OT split pattern.
