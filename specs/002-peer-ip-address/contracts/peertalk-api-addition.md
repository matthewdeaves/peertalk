# API Contract Addition: PT_PeerAddress

**Adds to**: `specs/001-peertalk-sdk/contracts/peertalk-api.md`

## New Function

### Peer Info (now 5, was 4)

```c
const char *PT_PeerAddress(PT_Peer *peer);
```
- Returns the peer's IP address as a dotted-quad string (e.g., "10.188.1.213")
- String is null-terminated, stored in the peer struct (valid for peer's lifetime)
- Returns "" (empty string) if peer is NULL
- Available in all peer states (discovered, connected, disconnected)
- No memory allocation — string is pre-formatted when IP is first set
- C89-compatible, no platform-specific types in signature

## Usage Pattern

```c
PT_Peer *peer = PT_GetPeer(ctx, i);
const char *name = PT_PeerName(peer);
const char *addr = PT_PeerAddress(peer);
/* Display: "PlayerOne@10.188.1.213" */
printf("%s@%s", name, addr);
```

## Updated Function Count

Total API: 22 functions (was 21)
- Lifecycle: 2
- Discovery: 2
- Connections: 2
- Messaging: 3
- Event Loop: 1
- Callback Registration: 6
- Configuration: 1
- Peer Info: 5 (was 4)
