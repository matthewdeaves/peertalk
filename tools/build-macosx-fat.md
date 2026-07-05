# Fat (universal) Mac OS X build

`tools/build-macosx-fat.sh` builds PeerTalk's test/demo apps as a single
**universal Mach-O** with a **PPC** slice and an **i386** slice, so one
binary runs on any Mac OS X 10.4–10.7 machine, PowerPC or Intel. dyld picks
the right slice per host CPU automatically.

This bridges the three eras PeerTalk targets: Classic Mac (MacTCP/OT),
OS X (this build), and modern POSIX — all speaking the same discovery /
TCP / UDP wire protocol.

## Why it's cheap

Darwin *is* BSD, so the POSIX backend (`src/platform/posix/pt_posix.c`)
compiles as-is. The only source concessions to OS X are small and already
in the tree:

- `pt_posix.c` — `MSG_NOSIGNAL` (Linux-only) falls back to `0` and each
  TCP socket gets `SO_NOSIGPIPE` instead (Darwin/BSD). No global SIGPIPE
  handler; the SDK never touches process-wide signal state.
- `tests/test_fast.c` — `clock_gettime(CLOCK_MONOTONIC)` didn't reach
  macOS until 10.12, so the OS X path uses `gettimeofday()`.

No new backend, no OS X-specific SDK code (Constitution IX).

## Build host

An **Intel Mac running OS X 10.7 Lion with Xcode 3.2.6 / 4.x** and the
legacy SDKs installed:

- `/Developer/SDKs/MacOSX10.4u.sdk` — the canonical *universal* SDK
  (PPC + i386); this is the default sysroot.
- `/usr/bin/gcc-4.0` — the 10.4u-compatible compiler (default `CC`).

The host has **no CMake and no git**, so the build is a standalone shell
script driven over SSH + rsync, not a CMake target. (`ppc + i386` covers
10.4–10.7 on both architectures; Intel Macs did not exist before 10.4, so
10.4 is the floor for a fat binary. A PPC-only build could reach 10.3.)

## Running it from a Linux dev box

```bash
# 1. push sources to the build host (git/cmake not required there).
#    NOTE: anchor the build-dir excludes with a leading slash, or the
#    unanchored glob also drops tools/build-macosx-fat.sh.
rsync -az --protocol=29 --exclude '.git' \
  --exclude '/build' --exclude '/build-*' --exclude '*.o' \
  ./ mini-intel:pt-fat/peertalk/
rsync -az --protocol=29 ~/clog/ mini-intel:pt-fat/clog/

# 2. build all five gate apps as fat binaries on the host.
ssh mini-intel 'cd pt-fat/peertalk && \
  CLOG_DIR=$HOME/pt-fat/clog bash tools/build-macosx-fat.sh'

# 3. sanity-check a slice list.
ssh mini-intel 'lipo -info pt-fat/peertalk/build-macosx-fat/test_lifecycle'
#   -> Architectures in the fat file: ... are: ppc i386
```

`build-macosx-fat.sh [APP ...]` builds the named apps (default: all five
POSIX gate apps). Env knobs: `CLOG_DIR`, `SDK`, `MIN`, `CC`, `ARCHS`, `OUT`.

## Known warnings (all third-party, not PeerTalk)

All `src/` sources compile warning-clean. Two residual warnings come from
outside the SDK and cannot be fixed from this repo:

- `clog_posix.c:178: implicit declaration of 'fsync'` — clog (external
  dep, Principle VII) omits `<unistd.h>`; `fsync` returns `int` and is
  called correctly, so this is benign. Fix belongs in the clog repo.
- `ld: ... crt1.o ... -mlong-branch ... no longer needed` — Apple's own
  startup object in the 10.4u SDK, emitted by the newer Lion linker.

The clog header's `#pragma GCC diagnostic push/pop` (unknown to gcc-4.0)
is silenced by including clog via `-isystem`.

## Runtime verification

- **Intel (i386 slice), OS X 10.7.5** — `test_lifecycle` ran on the Lion
  mini against a POSIX peer on Linux x86-64: full discover → connect →
  disconnect → reconnect → peer-lost, `Connects: 2, Disconnects: 2
  *** PASS ***` on both sides. `Platform: Darwin 11.4.2 i386`.
- **PPC slice** — present and lipo-verified in every fat binary, compiled
  with the powerpc backend, but not yet run on real PPC hardware. Pending:
  execute on the iMac G5 (10.5 PPC) once SSH/login access to it is set up.
