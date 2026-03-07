# peertalk Development Guidelines

## Constitution (Binding)

These 10 principles govern ALL implementation decisions. Full text: `.specify/memory/constitution.md`

1. **Three Apps Are the Spec** — Every feature MUST serve Bomberman, Chess, or Chat. If none need it, don't build it.
2. **SDK Handles the Protocol** — Framing, chunking, transport selection, discovery are invisible to the app.
3. **Honest About Platform Limits** — Measure on real hardware, document honestly. Never assume.
4. **Simple Defaults, No Knobs** — One TCP + one UDP per peer. No config structs. Add a setter only if an app needs tuning.
5. **Pre-Allocate Everything** — Zero malloc after PT_Init. All buffers allocated at init.
6. **Adapt at Init, Not Runtime** — FreeMem() at startup sizes buffers. No runtime adaptation or capability negotiation.
7. **Logging Is Separate** — clog is an external dependency, never exposed in peertalk.h.
8. **Test Apps and Demo Apps Prove the SDK** — Four test apps exercise all three app patterns. Demo apps (csend-pt) prove the SDK with real applications.
9. **Keep It Small** — Target under 15,000 lines total across all platforms.
10. **C89 for Portability** — All SDK code MUST be C89/C90. Test apps (POSIX only) may use C11.

## Before Every Change

- [ ] Does this serve Bomberman, Chess, or Chat? (I) — if no, don't build it
- [ ] Am I adding config knobs or options? (IV) — if yes, stop
- [ ] Does this allocate after init? (V) — if yes, redesign
- [ ] Is this C89-clean in SDK code? (X) — if not, fix it

## Project Structure

```
include/peertalk.h          # Single public header (C89, 21 functions)
src/core/                   # Platform-independent core
src/platform/posix/         # BSD sockets + select()
src/platform/mactcp/        # MacTCP async parameter blocks (68k)
src/platform/opentransport/ # OT endpoints + notifiers (PPC)
tests/                      # Four test apps (C11 on POSIX)
specs/001-peertalk-sdk/     # Spec, plan, tasks, contracts, research
```

## Build Commands

clog dependency: `~/Desktop/clog` — must be built first for each target platform.

```bash
# POSIX (build/)
mkdir -p build && cd build && cmake .. -DCLOG_DIR=$HOME/Desktop/clog && make

# 68k MacTCP (build-68k/) — for Mac SE
mkdir -p build-68k && cd build-68k && \
cmake .. -DCMAKE_TOOLCHAIN_FILE=~/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake \
  -DPT_PLATFORM=MACTCP -DCLOG_DIR=~/Desktop/clog -DCLOG_LIB_DIR=~/Desktop/clog/build-m68k && make

# PPC Open Transport (build-ppc-ot/) — for Performa 6400
mkdir -p build-ppc-ot && cd build-ppc-ot && \
cmake .. -DCMAKE_TOOLCHAIN_FILE=~/Retro68-build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
  -DPT_PLATFORM=OT -DCLOG_DIR=~/Desktop/clog -DCLOG_LIB_DIR=~/Desktop/clog/build-ppc && make

# 68k Open Transport (build-68k-ot/) — for Performa 630
mkdir -p build-68k-ot && cd build-68k-ot && \
cmake .. -DCMAKE_TOOLCHAIN_FILE=~/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake \
  -DPT_PLATFORM=OT -DCLOG_DIR=~/Desktop/clog -DCLOG_LIB_DIR=~/Desktop/clog/build-m68k && make

# PPC MacTCP (build-ppc-mactcp/) — for Performa 6200
mkdir -p build-ppc-mactcp && cd build-ppc-mactcp && \
cmake .. -DCMAKE_TOOLCHAIN_FILE=~/Retro68-build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
  -DPT_PLATFORM=MACTCP -DCLOG_DIR=~/Desktop/clog -DCLOG_LIB_DIR=~/Desktop/clog/build-ppc && make
```

## Code Style

- C89/C90: no `//` comments, no mixed declarations, no VLAs, no stdint.h in public header
- Zero malloc after PT_Init — all memory pre-allocated in single block
- ISR/ASR safety: set volatile flags only, process in main loop (see `.claude/rules/isr-safety.md`)
- Poll-based I/O on all platforms — no threads, no completion routines

## Known Platform Gotchas

**C89 + variadic macros**: clog uses variadic macros (C99). Do NOT use `-pedantic` — it rejects them. Use `-Wall -Wextra` only.

**POSIX C89 code**: `vsnprintf` requires `#define _POSIX_C_SOURCE 200112L` before includes when compiling with `-std=c89`.

**Classic Mac test apps** (R11, R17, R18): Retro68/LaunchAPPL console apps have no Toolbox init. Use `Delay()` for sleep (no Toolbox needed). Use `TickCount()/60` for timing (safe at main loop time, NOT at interrupt time). No stdio on Classic Mac — use clog with `clog_set_file("PT_Log")`. Do NOT call WaitNextEvent without Toolbox init (bus error in `_PortToMap`). Do NOT do Toolbox init before Retro68 console init (kills printf window). **MaxApplZone()/MoreMasters()** MUST be called before ANY Memory Manager or File Manager call — in test_init_toolbox() before clog_set_file, AND in PT_Init() before NewPtrClear. **No malloc after PT_Init** on Classic Mac — test apps must use static buffers. **Use CLOG_INFO** for test progress (printf may not reach LaunchAPPL).

**OT linker** (R12, R46): PPC builds link `OpenTransportAppPPC` + `OpenTransportLib` + `OpenTptInternetLib`. 68k OT builds link `OpenTransportApp` + `OpenTransport` + `OpenTptInet` + `ot_slm_stubs` (provides SLM dispatch symbols missing from Retro68 import libs). OT headers `#define` non-InContext names as InContext macros — add `#undef OTOpenEndpoint`, `#undef InitOpenTransport`, `#undef CloseOpenTransport` after OT includes.

**PPC toolchain**: File is `retroppc.toolchain.cmake`, NOT `retro68.toolchain.cmake`. `CMAKE_SYSTEM_NAME` is `RetroPPC` (not `Retro68`).

## Spec Artifacts

All design docs live in `specs/001-peertalk-sdk/`:
- `spec.md` — requirements and user stories
- `tasks.md` — 148 tasks across 26 phases (147 complete)
- `contracts/peertalk-api.md` — 21-function public API contract
- `research.md` — platform research decisions (R1-R46)

<!-- MANUAL ADDITIONS START -->
<!-- MANUAL ADDITIONS END -->
