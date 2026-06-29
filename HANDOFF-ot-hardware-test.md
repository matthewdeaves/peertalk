# Handoff: OpenTransport hardware test for the event-driven seam

**Branch:** `refactor/event-driven-platform-seam` (pushed to origin)
**Status as of 2026-06-29:** POSIX tested, MacTCP hardware-validated, seam
unit tests green on the host, **OT built clean but NOT yet run on real
hardware**. This doc is the pickup point for testing OT on a Performa 6400
(or other PPC OT Mac) once it's on the network.

---

## What this work is

Pulled the connection state machine out of each platform backend's
`poll()` so adapters only *emit events* and core owns every lifecycle
transition in one place.

- `PT_Event {CONNECTED/DATA/CLOSED, peer, ok}` + a `next_event()` vtable
  slot in `pt_internal.h`.
- `PT_Poll` drains it: `while next_event(&ev) pt_apply_platform_event(&ev)`.
- Two core-owned transitions: `pt_complete_connect()` and
  `pt_drain_disconnect()` in `pt_core.c`.
- All three backends ported to `next_event()` iterators (`ev_started`
  gate + `ev_cursor`, one event per call).

**Honest framing:** this did *not* reduce total code (backends −37 lines,
core machinery +115). The win is one place to get the lifecycle right,
plus `tests/test_seam.c` which can now verify it without hardware.

### Commits
- `4abef22` — the event-driven seam (core + 3 backends + test_seam + the
  `-Wempty-body` fix)
- `0e2ea64` — the Docker two-peer test track (`tools/two-peer-test.sh`)

---

## Current state

| Backend | Code | Hardware test |
|---------|------|---------------|
| POSIX (`pt_posix.c`) | done | PASS (gate suite) |
| MacTCP (`pt_mactcp.c`) | done | **PASS on real Mac (.213)** — lifecycle, chat, reliable, fast, multi |
| OT (`pt_ot.c`) | done, builds clean PPC + 68k | **NOT YET RUN** ← this handoff |

The seam unit test (`build/test_seam`, 23 checks, all PASS) already proves
the **platform-independent** parts for OT: the QUIT-vs-ERROR decision, and
the "buffered message + goodbye in one final read → message delivered THEN
clean QUIT" property. So OT hardware testing only needs to confirm the
**OT-specific** mechanics (see Risks below).

---

## How to test OT on hardware

This mirrors exactly the MacTCP procedure that passed on 2026-06-29.

### 1. Register the OT Mac in the MCP

The `classic-mac-hardware` MCP reads `machines.json`. The MacTCP Mac is
registered as id `mactcp` (10.188.1.213). Add the OT Mac similarly, e.g.
id `ot` / its IP, platform PPC. Then confirm:

```
list_machines
test_connection(machine="ot", test_launchappl=true)   # expect port 1984 open
```

LaunchAPPL must be running on the Mac (the Retro68 application launcher).

### 2. Build the OT test apps (PPC)

```bash
mkdir -p build-ppc-ot && cd build-ppc-ot
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
  -DPT_PLATFORM=OT -DCLOG_DIR=$HOME/clog
make            # builds all 5 test apps -> build-ppc-ot/test_*.bin
```

(For a 68k OT Mac use the `m68k-apple-macos/.../retro68.toolchain.cmake`
toolchain into `build-68k-ot/` instead. Both already configure + build
clean — only the documented `pt_memcpy_isr` warning.)

### 3. Start a POSIX partner peer on this host

One POSIX peer per host (two can't share TCP 7354 — no SO_REUSEADDR on the
listener). Same /24 as the Mac so UDP broadcast discovery reaches it
(NOT Docker — that's a different subnet).

```bash
cd build && make test_lifecycle
./test_lifecycle posix-host > /tmp/ot_peer.log 2>&1 &
```

### 4. Launch on the Mac via MCP, then read the POSIX log

```
execute_binary(machine="ot", platform="ppc",
               binary_path="<abs>/build-ppc-ot/test_lifecycle.bin")
```

Then `cat /tmp/ot_peer.log` — **the POSIX peer's log is the verdict**, not
the MCP return (FTP isn't configured on these Macs, so the Mac's own
`PT_Log` can't be pulled; and `test_multi` runs ~70s which exceeds the
LaunchAPPL ~45s window, so it will "time out" in the harness while the app
actually completes — read the POSIX log).

Expect, same as MacTCP: discovery both ways, connect, **disconnect reason
= QUIT** (clean goodbye, not timeout/error), reconnect, `*** PASS ***`.

### 5. Run the rest of the gate suite

Repeat steps 3–4 for `test_chat`, `test_reliable`, `test_fast`,
`test_multi` (each: POSIX partner of the same name + the matching
`build-ppc-ot/test_*.bin`). Look for:

- **chat:** received messages marked VALID, `Integrity: ok` (proves
  chunking/reassembly through the DATA event path)
- **reliable:** `Order valid: yes`, `Payload valid: yes`
- **fast:** UDP messages received, `Payload valid: yes`, oversize rejected
- **multi:** `Broadcasts recv >= 1`

---

## OT-specific risks to watch

These are the bits `test_seam` and the MacTCP pass do **not** cover —
where an OT-only bug would hide:

1. **XTI listener drain / T_LISTEN deadlock.** This is where OT bit the
   project before (v1.11.0: stale T_DISCONNECT on the listener blocked all
   future T_LISTEN). The handling moved into `ot_round_start` — logic
   unchanged, but new call structure. **Watch:** does the Mac accept a
   *second* incoming connection (the reconnect in lifecycle, and multi)?
   A hang there is the classic symptom.

2. **Half-close ordering.** `OTRcvOrderlyDisconnect`/
   `OTSndOrderlyDisconnect` now run in `ot_next_event` *before* emitting
   CLOSED, and core does the goodbye-parsing the old poll did inline (with
   its R4 owner re-read). **Watch:** disconnect reason should be QUIT, not
   ERROR — an ERROR means the buffered goodbye was lost across the
   orderly-release path.

3. **Notifier/interrupt timing** on the endpoint flags — only exercisable
   on real OT. A dropped or doubled event shows up as a missed callback or
   a spurious disconnect.

If OT misbehaves, the change is isolated to `pt_ot.c` (`ot_poll` →
`ot_round_start` + `ot_next_event` + `ot_recv_into_peer`). The previous,
hardware-proven `ot_poll()` is in git history at `cf9df64` for reference
or a quick A/B.

---

## Where to look in the code

- `src/core/pt_core.c` — `pt_complete_connect`, `pt_drain_disconnect`,
  `pt_apply_platform_event`, the `PT_Poll` drain loop
- `src/platform/opentransport/pt_ot.c` — `ot_round_start`,
  `ot_next_event`, `ot_recv_into_peer`
- `src/platform/mactcp/pt_mactcp.c` — `mactcp_next_event` (the proven
  reference for the same pattern)
- `tests/test_seam.c` — host-side seam tests (run `build/test_seam`)
- Memory: `event-seam-refactor-plan` (auto-memory) has the running log.
