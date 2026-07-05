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

## Runtime verification — full OS X hardware matrix, all PASS

`test_lifecycle` run against a POSIX peer on the Linux host, both sides
`Connects: 2, Disconnects: 2 *** PASS ***`:

| Mac | OS / CPU | Build | Result |
|-----|----------|-------|--------|
| Intel mini (`mini-intel`) | 10.7.5 / i386 | fat (min 10.4) | PASS |
| iMac G5 (`imac-g5`) | 10.5.8 / ppc970 | fat ppc slice | PASS |
| G4 Quicksilver (`quicksilver`) | 10.4.11 / ppc7450 | fat ppc slice | PASS |
| G3 Yosemite (`yosemite`) | 10.3.9 / ppc750 | **ppc-only, 10.3.9** | PASS |

The G3 runs 10.3.9, below the fat binary's 10.4 floor, so it needs a
PPC-only build against the 10.3.9 SDK (Intel didn't exist pre-10.4):

```bash
SDK=/Developer/SDKs/MacOSX10.3.9.sdk MIN=10.3.9 ARCHS=ppc \
  OUT=build-macosx-ppc103 CLOG_DIR=~/pt-fat/clog \
  bash tools/build-macosx-fat.sh
```

## Seeing it on the Mac's screen

These are native **console** apps, so `ssh host ./test_lifecycle` runs them
headless (stdout returns over SSH). To watch one run **on the Mac's own
display**, `tools/osx-screen-run.sh <host> [app]` opens a Terminal window on
the logged-in desktop via `osascript`. Works on every OS X Mac (Intel + PPC)
because it uses the supported POSIX/BSD-sockets build.

A *native app window* on OS X is the Retro68 **Carbon** path (LaunchAPPL +
a Carbon build of the test apps, using the Open Transport backend — the
Retro68 Carbon SDK exposes `OpenTransport.h`, and OT does run for Carbon
apps on PPC OS X). That build is tracked/added separately; see the Carbon
build target. On the Intel mini (10.7, no Carbon PPC), the Terminal launcher
is the on-screen path.
