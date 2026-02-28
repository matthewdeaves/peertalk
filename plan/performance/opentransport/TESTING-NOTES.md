# Open Transport Testing Notes

## Performa 6400 (PPC, Mac OS 8.1, Open Transport)

### Hardware Testing Session - 2026-02-27

#### Test Results

| Test | Status | Notes |
|------|--------|-------|
| Discovery | PASS | First test, worked immediately |
| Latency | PASS | 500/502 pings, 0 lost. Avg: 16B=0ms, 64B=50ms, 256B=83ms, 1024B=216ms, 4096B=783ms |
| Throughput | PASS (post-fix) | All sizes flowing. 3-6 KB/s send, 3-5 KB/s recv |
| Stress | PASS | 5/5 cycles, 0 failures, 100% success, zero memory leaks |
| Stream | PASS | SEND 5-6 KB/s, RECV 33-118 KB/s. All sizes working. |

#### Bugs Found & Fixed

##### 1. Send Queue Never Allocated on Connect (FIXED)
- **Files:** `src/opentransport/tcp_connect_ot.c`, `src/opentransport/tcp_server_ot.c`
- **Symptom:** "no send queue" errors filled PT_Log (301KB of errors)
- **Root Cause:** Neither outbound `pt_ot_connect_complete()` nor inbound accept path
  allocated `peer->send_queue` / `peer->recv_queue` on connection establishment
- **Fix:** Added queue allocation (16 slots) in both connect and accept completion paths

##### 2. Send Queue Never Drained in Poll Loop (FIXED)
- **Files:** `src/opentransport/poll_ot.c`
- **Symptom:** Messages queued to send_queue never sent to network
- **Root Cause:** `pt_ot_poll_connected()` was missing Tier 1 (queue-based, up to 8 msgs/poll)
  and Tier 2 (direct buffer) send draining. Both exist in MacTCP `poll_mactcp.c` but were
  missing from the OT poll loop.
- **Fix:** Added Tier 1 queue drain loop with rollback on flow control, and Tier 2 direct
  buffer send path to `pt_ot_poll_connected()`

##### 3. Protocol Framing Missing for OT Sends (FIXED)
- **Files:** `src/opentransport/poll_ot.c`
- **Symptom:** Raw payload sent without protocol headers; partner couldn't parse messages
- **Root Cause:** OT lacks scatter-gather (no iovec), so sends need manual framing into obuf
- **Fix:** Added `pt_ot_send_framed()` that frames raw payload with compact (4-byte) or
  full (10-byte + 2-byte CRC) headers into peer's obuf before OTSnd

##### 4. Callback Ordering Bug - Disconnect (FIXED)
- **Files:** `src/opentransport/poll_ot.c` (5 paths), `src/opentransport/ot_multi.c` (5 paths)
- **Symptom:** `PeerTalk_Connect` in `on_peer_disconnected` callback fails with PT_ERR_RESOURCE (-10)
- **Root Cause:** `on_peer_disconnected` callback fired BEFORE endpoint cleanup. Apps like
  `test_latency.c` call `PeerTalk_Connect` from the callback, which fails because endpoint pool
  is still full and peer state is still CONNECTED.
- **Fix:** Moved ALL cleanup (free queues, clear connection, set state DISCOVERED, cleanup endpoint)
  BEFORE the callback in all 10 disconnect paths (5 TCP, 5 ADSP). Matches MacTCP pattern which
  has explicit "CRITICAL" comments about this ordering.

##### 5. T_DATA Race Condition (FIXED)
- **Files:** `src/opentransport/poll_ot.c`, `src/opentransport/ot_multi.c`
- **Symptom:** Throughput stalls on 2048B+ messages; recv stops after initial burst
- **Root Cause:** `PT_OT_FLAG_DATA_AVAILABLE` cleared AFTER OTRcv drains data. Race:
  OTRcv drains → new data arrives → notifier sets flag → code clears flag → notification LOST.
  Recv only resumes if another T_DATA happens to arrive.
- **Fix:** Removed flag-gating on recv; always try to read every poll cycle. OTRcv returns
  kOTNoDataErr cheaply when empty. Flag clearing moved to `pt_ot_tcp_recv()` on kOTNoDataErr
  only (already handled correctly there).

##### 6. Tier 2 Direct Buffer Data Loss on Flow Control (FIXED)
- **Files:** `src/opentransport/poll_ot.c`
- **Symptom:** 2048B/4096B messages silently dropped; sent count increments but data never arrives
- **Root Cause:** `pt_direct_buffer_complete()` called unconditionally after `pt_ot_send_framed()`,
  even when send returned `PT_ERR_WOULD_BLOCK` (kOTFlowErr). This transitions the buffer to IDLE,
  discarding the unsent data. The test app queues the next message, which also gets dropped.
- **Fix:** On `PT_ERR_WOULD_BLOCK`, transition buffer state back to `PT_DIRECT_QUEUED` instead of
  calling `pt_direct_buffer_complete()`. Buffer retried on next poll cycle.

##### 7. Local Disconnect Missing Full Peer Cleanup (FIXED)
- **Files:** `src/opentransport/tcp_connect_ot.c`
- **Symptom:** Stress test cycle 2+ fails with PT_ERR_RESOURCE (-10); can't reconnect
- **Root Cause:** `pt_ot_disconnect()` only called `pt_ot_tcp_close()` and set
  `peer->hot.connection = NULL`, but didn't free queues, clear `hot->peer`, or set
  peer state to DISCOVERED. The endpoint enters PT_EP_CLOSING (async) but the peer
  is left in CONNECTED with no way to reconnect.
- **Fix:** Added full peer cleanup in `pt_ot_disconnect()`: clear `hot->peer`, free
  send/recv queues, set peer state to DISCOVERED. Endpoint cleanup finishes async.

##### 8. State Validator Rejects CONNECTED → DISCOVERED Transition (FIXED)
- **Files:** `src/core/peer.c`
- **Symptom:** "Invalid state transition: CONNECTED → DISCOVERED" warning, peer stuck
- **Root Cause:** `pt_peer_set_state()` only allowed CONNECTED → DISCONNECTING/FAILED/UNUSED.
  Local disconnect needs CONNECTED → DISCOVERED for immediate reconnection.
- **Fix:** Added DISCOVERED as valid target from CONNECTED state.

##### 9. LaunchAPPLServer Stuck Due to Orphaned Client Process (FIXED)
- **Files:** `.claude/mcp-servers/classic-mac-hardware/server.py`
- **Symptom:** LaunchAPPLServer unresponsive after test execution timeout
- **Root Cause:** `subprocess.run(..., timeout=60)` raises TimeoutExpired but does NOT
  kill the child LaunchAPPL process. The orphaned client stays connected to the server,
  and the server can't accept new connections (stuck in `connected = true`).
- **Fix:** Changed to `Popen` + `proc.communicate(timeout=60)` with explicit
  `proc.kill()` + `proc.wait()` on timeout. Server gets clean TCP RST and resets.
- **Note:** Fix requires Claude Code restart to take effect (MCP server reload).

#### Known Issues (Not Yet Fixed)

##### Log Streaming to Partner Fails
- **Symptom:** Partner rejects "LOG:" stream prefix as invalid message header
- **Impact:** Low - logs still saved to disk on Mac, downloadable via FTP
- **Status:** Deferred - not blocking test execution

##### OT SEND Throughput Much Lower Than MacTCP
- **Symptom:** OT SEND tops out at 5-6 KB/s vs MacTCP's 30-500 KB/s
- **Impact:** Medium - functional but slow
- **Likely Cause:** OT flow control cycle (kOTFlowErr → T_GODATA) adds latency per send
- **Status:** Optimization target for perf-optimize runs

#### Throughput Results

**Pre-fix (stalled at 2048B+):**
```
 256 bytes: SEND    3 KB/s  RECV    2 KB/s  (errs=0)
 512 bytes: SEND    3 KB/s  RECV    3 KB/s  (errs=0)
1024 bytes: SEND    5 KB/s  RECV    5 KB/s  (errs=0)
2048 bytes: SEND    1 KB/s  RECV    0 KB/s  (errs=0)  ← STALLED
4096 bytes: SEND    1 KB/s  RECV    0 KB/s  (errs=0)  ← STALLED
```

**Post-fix (T_DATA race + Tier 2 flow control):**
```
 256 bytes: SEND    3 KB/s  RECV    3 KB/s  (errs=0)
 512 bytes: SEND    4 KB/s  RECV    4 KB/s  (errs=0)
1024 bytes: SEND    5 KB/s  RECV    5 KB/s  (errs=0)
2048 bytes: SEND    5 KB/s  RECV    5 KB/s  (errs=0)  ← FIXED
4096 bytes: SEND    6 KB/s  RECV    5 KB/s  (errs=0)  ← FIXED
```

#### Stream Test Results (One-Way)
```
 256 bytes: SEND    5 KB/s  RECV   33 KB/s
 512 bytes: SEND    5 KB/s  RECV   22 KB/s
1024 bytes: SEND    6 KB/s  RECV   44 KB/s
2048 bytes: SEND    5 KB/s  RECV   59 KB/s
4096 bytes: SEND    6 KB/s  RECV  118 KB/s
```

RECV throughput scales well with message size (linear increase 33→118 KB/s).
SEND throughput is consistent at 5-6 KB/s across all sizes - likely limited by
OT TCP send buffer and flow control recovery cycle.

#### Stress Test Results
- 5/5 connect/disconnect cycles: 100% success
- 0 connection failures
- Zero memory leaks (Initial FreeMem = Final FreeMem = 2,266,320)
- Log streaming reconnection also works

#### Debug Log Analysis (PT_LibDebug, 2361 lines, 134KB)
- Flow control (`kOTFlowErr`) hit immediately at 2048B message size
- Every Tier 2 send attempt returned flow control for rest of test phase
- "Tier 2 buffer busy" messages indicate rapid queuing while buffer occupied
- Data arriving from partner (T_DATA events logged) but flag race prevented recv
- 2048B: Only 16 messages sent (initial burst), 12 received. Then stalled.
- 4096B: Only 9 messages sent, 6 received. Same pattern.

#### MacTCP Comparison (Performa 6200)
For reference, MacTCP on Performa 6200 (8MB RAM, 68k) achieved much higher throughput:
```
Stream test (one-way):
 256 B: SEND 29-32 KB/s,  RECV 22 KB/s
 512 B: SEND 93-97 KB/s,  RECV 22 KB/s
1024 B: SEND 171-190 KB/s, RECV 44 KB/s
2048 B: SEND 321-328 KB/s, RECV 59 KB/s
4096 B: SEND 497-499 KB/s, RECV 117-118 KB/s
```
OT throughput should be comparable or better once fixes are verified.
