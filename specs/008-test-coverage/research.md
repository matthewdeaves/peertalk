# Research: Test Coverage Gaps

## R1: Multi-Peer SDK Support

**Decision**: The SDK fully supports multi-peer. No SDK changes needed for the multi-peer test.

**Findings**:
- `PT_Connect` operates per-peer, no single-connection guard (pt_core.c:440)
- Tiebreaker is per-pair (local_ip vs peer_ip), resolves independently for each pair (pt_core.c:186-204)
- `PT_Broadcast` loops all peer slots, sends to every connected peer (pt_messaging.c:153)
- `PT_GetPeerCount` / `PT_GetPeer` iterate in_use slots (pt_core.c:628)
- Mac SE with 4MB RAM can handle 30+ peer slots; 3 peers = ~54KB for TCP streams

**Risk**: MacTCP `re_listen()` only re-issues ONE passive open per poll call. With 3 simultaneous incoming connections, the listener needs multiple poll cycles to re-listen after each accept. This is fine since poll runs every 16ms and TCP handshakes take ~100ms.

## R2: Error Path Return Values

**Decision**: Error path tests can validate exact return codes. All are documented.

**Findings**:
- `PT_Send(ctx, NULL, ...)` → `PT_ERR_INVALID_ARG` (pt_messaging.c:73)
- `PT_Send(ctx, peer, ...)` where peer not connected → `PT_ERR_NOT_CONNECTED` (pt_messaging.c:75)
- `PT_Send(ctx, peer, ..., NULL, len>0)` → `PT_ERR_INVALID_ARG` (pt_messaging.c:74)
- `PT_Connect(ctx, NULL)` → `PT_ERR_INVALID_ARG` (pt_core.c:446)
- `PT_Connect(ctx, already_connected_peer)` → `PT_ERR_NOT_CONNECTED` (pt_core.c:448-451)
- `PT_Broadcast(ctx, ...) with no peers` → `PT_OK` (pt_messaging.c:164)
- `PT_Broadcast(NULL, ...)` → `PT_ERR_INVALID_ARG` (pt_messaging.c:151)

## R3: Multi-Peer Test Design

**Decision**: test_multi uses a phased approach with generous timeouts for the Mac SE.

**Design**:
1. **Discovery phase** (45s): Wait for N-1 peers to be discovered (N configurable, default 1 = 2-peer mode)
2. **Connect phase**: Auto-connect to all discovered peers via on_discovered callback
3. **Settle phase** (10s): Wait for all connections to complete, allow tiebreakers to resolve
4. **Message phase**: Each peer broadcasts MSG_CHAT "HELLO from <name>". Wait for broadcasts from all connected peers.
5. **Disconnect phase** (5s grace): Disconnect all peers cleanly.

**PASS criteria**: connected_count >= expected_peers AND broadcast_received >= expected_peers AND all disconnects clean (QUIT, not ERROR)

**Solo mode**: If only 1 peer connects (or 0), the test still passes the subset — useful for 2-machine testing. The "expected peers" count is based on actual discovered peers, not a hardcoded 3.

## R4: PT_StopDiscovery / PT_StartDiscovery in test_lifecycle

**Decision**: Add a stop/restart phase between the first and second connection cycles.

**Flow change**:
1. First connect + disconnect (existing)
2. Call PT_StopDiscovery, wait 5s, verify g_peers_discovered doesn't increase
3. Call PT_StartDiscovery, wait for re-discovery
4. Second connect + disconnect (existing)

**Concern**: The 5s stop window adds time. On Mac SE at 8MHz this is acceptable.

## R5: PT_SetName in test_lifecycle

**Decision**: Call PT_SetName before the second connection cycle. Verify the remote peer sees the new name.

**Implementation**: After PT_StopDiscovery/PT_StartDiscovery, call `PT_SetName(ctx, "NewName")`. In on_discovered, check `PT_PeerName(peer)` — the remote peer's name comes from discovery broadcasts, so we're checking that our renamed peer's name propagates to the remote side. Actually, PT_SetName changes OUR name; the remote peer would see our new name in their on_discovered callback. We can verify locally by checking that PT_SetName doesn't crash and that subsequent discovery still works.

## R6: PT_OnPeerLost Validation

**Decision**: Add peer-lost tracking to test_lifecycle. Accept the 15s timeout cost.

**Implementation**: Register on_peer_lost callback, increment g_peers_lost counter. After the final disconnect, stop discovery and wait 15s for the peer-lost callback to fire. Add g_peers_lost >= 1 to PASS criteria.

**Concern**: This adds 15s to the test. For hardware testing this is acceptable. Can be skipped in quick-test modes by not waiting for the timeout.
