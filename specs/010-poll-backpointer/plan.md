# Implementation Plan: Poll Back-Pointer Optimisation

**Branch**: `010-poll-backpointer` | **Date**: 2026-04-06 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/010-poll-backpointer/spec.md`

## Summary

Add a back-pointer (`PT_Peer_Internal *owner`) to MacTCP's `TCPStreamSlot` and OT's `OTEndpointSlot`, replacing O(max_peers) linear scans in the poll hot path with O(1) pointer dereferences. The `find_peer_for_stream` and `find_peer_for_ep` helper functions are removed. All existing tests must continue to pass on all platforms with no behavioural changes.

## Technical Context

**Language/Version**: C89/C90 (SDK), C11 (POSIX test apps)  
**Primary Dependencies**: clog (logging), MacTCP (68k), Open Transport (PPC), BSD sockets (POSIX)  
**Storage**: N/A  
**Testing**: Four test apps (test_init_only, test_lifecycle, test_fast, test_reliable) on POSIX + Classic Mac hardware  
**Target Platform**: POSIX (Linux/macOS), MacTCP (68k System 6-7.5), Open Transport (PPC System 7.6+)  
**Project Type**: Library (static)  
**Performance Goals**: Reduce per-poll CPU cost on 68k/PPC by eliminating repeated linear scans  
**Constraints**: Zero malloc after PT_Init, C89 in SDK code, under 15,000 LOC total  
**Scale/Scope**: 2-32 peers, ~4,400 LOC currently

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Gate | Status |
|-----------|------|--------|
| I. Three Apps Are the Spec | Does this serve Bomberman, Chess, or Chat? | PASS — Chat (TCP-heavy polling), Chess (connection lifecycle), Bomberman (reconnect cycles) |
| II. SDK Handles the Protocol | Is the change internal to the SDK? | PASS — no public API change, purely internal optimisation |
| III. Honest About Platform Limits | Are we measuring, not assuming? | PASS — optimisation motivated by code review, impact is structural (O(n)→O(1)) not speculative |
| IV. Simple Defaults, No Knobs | Are we adding config knobs? | PASS — no new configuration, no new API surface |
| V. Pre-Allocate Everything | Does this allocate after init? | PASS — back-pointer is a field in existing pre-allocated structs |
| VI. Adapt at Init, Not Runtime | Any runtime adaptation? | PASS — no runtime adaptation added |
| VII. Logging Is Separate | Any clog exposure? | PASS — no change to logging |
| VIII. Test Apps Prove the SDK | Do existing tests cover this? | PASS — all 4 test apps exercise the affected code paths |
| IX. Keep It Small | Line count impact? | PASS — net reduction expected (removing ~20 lines of scan functions, adding ~4 lines of struct fields) |
| X. C89 for Portability | Is SDK code C89? | PASS — pointer field declaration is C89 |

**Gate result: ALL PASS — no violations, no complexity tracking needed.**

## Project Structure

### Documentation (this feature)

```text
specs/010-poll-backpointer/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
src/platform/mactcp/pt_mactcp.c      # TCPStreamSlot gains owner field, find_peer_for_stream removed
src/platform/opentransport/pt_ot.c   # OTEndpointSlot gains owner field, find_peer_for_ep removed
```

No new files. No changes to core/, posix/, include/, or tests/.

**Structure Decision**: Modifications confined to two existing platform backend files. No new directories or files needed.
