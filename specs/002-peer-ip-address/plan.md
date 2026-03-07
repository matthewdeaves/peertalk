# Implementation Plan: Peer IP Address API

**Branch**: `002-peer-ip-address` | **Date**: 2026-03-07 | **Spec**: `specs/002-peer-ip-address/spec.md`
**Input**: Feature specification from `/specs/002-peer-ip-address/spec.md`

## Summary

Add `PT_PeerAddress()` to the public API, returning a peer's IP as a dotted-quad string. The IP is already stored internally as `unsigned long ip_addr` in network byte order. This feature adds a 16-byte `addr_str` field to `PT_Peer_Internal`, formats it once at discovery/connection time, and exposes it via a one-line accessor function.

## Technical Context

**Language/Version**: C89/C90
**Primary Dependencies**: None (no new dependencies)
**Storage**: N/A
**Testing**: Existing test apps (test_lifecycle) verify on real hardware
**Target Platform**: POSIX, MacTCP (68k), Open Transport (PPC)
**Project Type**: Library (static)
**Performance Goals**: Zero overhead on hot path (PT_Poll/PT_Send)
**Constraints**: No malloc after PT_Init, C89, no platform includes in public header
**Scale/Scope**: 1 new function, ~15 lines of implementation

## Constitution Check

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Three Apps Are the Spec | PASS | Chat app needs "username@ip" display |
| II. SDK Handles Protocol | PASS | Read-only accessor, no protocol change |
| III. Honest About Limits | N/A | No performance implications |
| IV. Simple Defaults, No Knobs | PASS | Single function, no configuration |
| V. Pre-Allocate Everything | PASS | 16-byte char array in existing struct, no malloc |
| VI. Adapt at Init | N/A | No sizing decisions |
| VII. Logging Separate | N/A | No logging changes |
| VIII. Test/Demo Apps Prove SDK | PASS | csend-pt demo app will use this |
| IX. Keep It Small | PASS | ~15 lines added |
| X. C89 Portability | PASS | Manual byte extraction, no inet_ntoa |

## Research

### R1: IP-to-string on Classic Mac (C89)

`inet_ntoa()` is not available in MacTCP headers. `inet_ntop()` is C99/POSIX.
The portable C89 solution is manual byte extraction from the network-order `unsigned long`:

```c
static void pt_format_ip(unsigned long ip, char *buf)
{
    unsigned char *b = (unsigned char *)&ip;
    /* ip is network byte order (big-endian), b[0] is most significant */
    int i;
    char *p = buf;
    for (i = 0; i < 4; i++) {
        unsigned char v = b[i];
        if (v >= 100) { *p++ = '0' + v / 100; v %= 100; *p++ = '0' + v / 10; v %= 10; }
        else if (v >= 10) { *p++ = '0' + v / 10; v %= 10; }
        *p++ = '0' + v;
        if (i < 3) *p++ = '.';
    }
    *p = '\0';
}
```

This works on all platforms: 68k (big-endian native), PPC (big-endian native), x86 POSIX (network byte order is big-endian). No byte-swap needed because `ip_addr` is already stored in network byte order.

Max output: "255.255.255.255" = 15 chars + null = 16 bytes.

### R2: Where to format

Format once when the IP is first set, not on every `PT_PeerAddress()` call. The IP is set in two places:
1. `pt_discovery_receive()` — when a new peer is discovered (`peer->ip_addr = source_ip`)
2. `pt_handle_incoming_connection()` — when an unknown peer connects (`peer->ip_addr = peer_ip`)

Both paths go through `pt_alloc_peer()` or `pt_find_peer_by_ip()`. The cleanest approach: add a helper `pt_set_peer_ip()` that sets both `ip_addr` and formats `addr_str`, then call it from both sites.

### R3: Storage

Add `char addr_str[16]` to `PT_Peer_Internal`. This is inside the pre-allocated peer array — no additional malloc. The 16 bytes per peer is negligible (32 peers = 512 bytes total).

## Project Structure

No new files. Changes to existing files only:

```
include/peertalk.h          # Add PT_PeerAddress declaration
src/core/pt_internal.h      # Add addr_str[16] to PT_Peer_Internal, declare pt_format_ip
src/core/pt_core.c          # Add PT_PeerAddress impl, pt_format_ip, pt_set_peer_ip helper
src/core/pt_discovery.c     # Use pt_set_peer_ip instead of direct ip_addr assignment
specs/002-peer-ip-address/  # This spec
```
