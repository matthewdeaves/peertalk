---
description: AppleTalk and ADSP-specific rules and patterns
paths:
  - "src/appletalk/**/*"
---

# AppleTalk Rules

Rules for AppleTalk networking (NBP discovery, ADSP connections) on any Mac with System 6+.

## ADSP Callbacks

ADSP has **TWO callback types**, both run at **interrupt level**. See `isr-safety.md` for general rules.

> **Verified:** Programming With AppleTalk (Lines 5924-5926):
> "userRoutine... **is called under the same conditions as a completion routine (at interrupt level) and must follow the same rules as a completion routine.**"

### Callback Types

| Callback | UPP Type | Parameter | Purpose |
|----------|----------|-----------|---------|
| `ioCompletion` | `ADSPCompletionUPP` | `DSPPBPtr` in A0 | Async I/O completion |
| `userRoutine` | `ADSPConnectionEventUPP` | `TPCCB` in A1 | Connection events |

### Register Preservation

> **Verified:** Programming With AppleTalk (Lines 1675-1678, 11544):
> - **D0, D1, D2, A0, A1** are saved by the interrupt handler
> - **D3-D7, A2-A6** must be preserved by your callback

### UPP Creation Required

```c
/* Create BOTH UPP types at init */
ADSPCompletionUPP g_completion_upp = NewADSPCompletionUPP(adsp_completion);
ADSPConnectionEventUPP g_event_upp = NewADSPConnectionEventUPP(adsp_event);

/* At shutdown */
DisposeADSPCompletionUPP(g_completion_upp);
DisposeADSPConnectionEventUPP(g_event_upp);
```

## CRITICAL: userFlags Must Be Cleared

> **Verified:** Programming With AppleTalk (Lines 5780-5782):
> "Once the attention message has been received, you must set the userFlags to zero. This allows another attention message, or other unsolicited connection event, to occur. **Failure to clear the userFlags will result in your connection hanging.**"

### userFlags Values

| Constant | Value | Meaning |
|----------|-------|---------|
| `eClosed` | 0x80 | Connection closed advice received |
| `eTearDown` | 0x40 | Connection broken |
| `eAttention` | 0x20 | Attention message received |
| `eFwdReset` | 0x10 | Forward reset advice received |

**Note:** There are NO data arrival flags. Data arrival is signaled through `ioCompletion`, not `userFlags`.

### userRoutine Pattern

```c
static pascal void adsp_event(TPCCB ccb) {
    pt_adsp_connection *conn = (pt_adsp_connection *)ccb;

    /* Read flags and IMMEDIATELY clear */
    uint8_t flags = ccb->userFlags;
    ccb->userFlags = 0;  /* CRITICAL: Must clear! */

    /* Now process captured flags */
    if (flags & eClosed)
        conn->flags.closed = 1;
    if (flags & eTearDown)
        conn->flags.torn_down = 1;
    if (flags & eAttention)
        conn->flags.attention = 1;
    if (flags & eFwdReset)
        conn->flags.fwd_reset = 1;
}
```

### ioCompletion Context Recovery

> **Verified:** Programming With AppleTalk (Lines 1565-1577, 1670-1673):
> "The routine is called at interrupt level, with the address of the parameter block in register A0"
> "extend the parameter block referred to by register A0 to include a long word immediately before the regular parameter block"

```c
/* Extended param block for context recovery */
typedef struct {
    void           *context;    /* 4 bytes before param block */
    DSPParamBlock   pb;         /* Actual param block (A0 points here) */
} pt_adsp_extended_pb;

#define PT_ADSP_GET_CONTEXT(pb) \
    (((pt_adsp_extended_pb *)((char *)(pb) - sizeof(void *)))->context)

static pascal void adsp_completion(DSPPBPtr pb) {
    pt_adsp_connection *conn = PT_ADSP_GET_CONTEXT(pb);
    conn->async_result = pb->ioResult;
    conn->async_pending = false;
}
```

### userRoutine Context Recovery

```c
/* TRCCB must be FIRST member of your connection struct */
typedef struct {
    TRCCB           ccb;        /* MUST be first */
    /* ... other fields ... */
} pt_adsp_connection;

/* In userRoutine, cast directly */
pt_adsp_connection *conn = (pt_adsp_connection *)ccb;
```

## Parameter Block Rules

> **Verified:** Programming With AppleTalk (Lines 1885-1887, 1906-1907, 1914-1915):

1. **Integrity:** "A parameter block's integrity must be maintained until the operation completes"
2. **Read-only during async:** "you must not write to a parameter block while it is being used by AppleTalk"
3. **Allocation:** "allocate the parameter blocks used with asynchronous calls using NewPtr or NewHandle with the handle locked"

### ioResult Field

> **Verified:** Programming With AppleTalk (Lines 5913-5915):
> "ioResult contains the result of the trap when it is finished. During asynchronous operation this field is first set to 1 (denoting that the trap is in process) then set to the final result code when the trap is completed."

## Complete ADSP Error Codes

> **Verified:** Programming With AppleTalk (Lines 5896-7229)

| Code | Name | Meaning |
|------|------|---------|
| 0 | noErr | Success |
| -91 | ddpSktErr | Error opening DDP socket |
| -1273 | errOpenDenied | Connection open denied by remote |
| -1274 | errDSPQueueSize | Send/receive queue too small (< 100 bytes) |
| -1276 | errAttention | Attention message > 570 bytes |
| -1277 | errOpening | Error during connection opening |
| -1278 | errState | Invalid state for this operation |
| -1279 | errAborted | Operation aborted |
| -1280 | errRefNum | Invalid connection reference number |

## Buffer Requirements

> **Verified:** Programming With AppleTalk (Lines 5927-5943)

| Buffer | Size | Notes |
|--------|------|-------|
| Send queue minimum | 100 bytes | Constant: `minDSPQueueSize` |
| Receive queue minimum | 100 bytes | Constant: `minDSPQueueSize` |
| Attention buffer | **exactly** 570 bytes | Constant: `attnBufSize` |
| Attention message data | up to 570 bytes | Line 5768 |

> **Error:** If queues are smaller than `minDSPQueueSize`, `errDSPQueueSize` (-1274) is returned.

## Connection Listener (dspCLListen)

> **Verified:** Programming With AppleTalk (Lines 6712-6714, 6780-6788)

```c
/* Almost always called asynchronously */
dspCLListen(&pb, true);  /* true = async */
```

When complete, parameter block contains:
- `remoteCID` - Connection ID of remote connection end
- `remoteAddress` - Socket address of remote end
- `sendSeq`, `sendWindow`, `attnSendSeq` - Synchronization info

Pass these to `dspOpen` or `dspCLDeny`.

## Connection State Polling

> **Verified:** Programming With AppleTalk (Lines 5153, 5175-5176, 5232-5233)

When `userRoutine` is NIL:
> "you must poll the userFlags field of the CCB for connection events"
> "check the state field of the CCB to see when it is set to sOpen"
> "your code must either poll the ioResult field or the state field of the CCB to determine when the connection has been opened"

## dspInit Must Be Called First

> **Verified:** Programming With AppleTalk (Line 5842):
> "dspInit is used to create and initialize a connection end. **It must be called before the connection is opened.**"

## NBP (Name Binding Protocol)

For peer discovery on AppleTalk networks.

### Name Format

> **Verified:** Programming With AppleTalk (Lines 2155-2160, 2619, 2645, 8986):

NBP names have three parts: `object:type@zone`
- **Object:** Up to **31** characters (your app/peer name)
- **Type:** Up to **31** characters (your app type, e.g., "PeerTalk")
- **Zone:** Up to **31** characters, or "*" for local zone

> **Note:** The limit is 31 characters, not 32. Line 8986: "they must be less than or equal to 31 characters in length"

### Registration

```c
/* Register name for discovery */
NBPRegister(&myEntity, false);  /* false = don't confirm */

/* Must unregister on quit */
NBPRemove(&myEntity);
```

### Lookup

```c
/* Search for peers */
NBPLookup(&lookupPB, false);  /* false = async */

/* Results in buffer as EntityName entries */
```

## LocalTalk vs EtherTalk

- LocalTalk: Slower (230.4 kbps), but works on any Mac
- EtherTalk: Faster, requires Ethernet hardware

Test on LocalTalk if possible - it exposes timing issues that EtherTalk hides.

## ADSP Connection States

> **Verified:** Programming With AppleTalk (Lines 5176, 13716)

| State | Meaning |
|-------|---------|
| sOpening | Connection is being established |
| sOpen | Connection is open and ready |
| sClosing | Connection is being closed |
| sClosed | Connection is closed |

Poll the `state` field of the CCB to detect state changes when not using userRoutine.

## ADSP Open Mode Constants

> **Verified:** Programming With AppleTalk

| Constant | Value | Purpose |
|----------|-------|---------|
| ocRequest | 1 | Initiate connection to remote end |
| ocPassive | 2 | Wait for incoming connection request |
| ocAccept | 3 | Accept connection (from connection listener) |
| ocEstablish | 4 | Full manual connection setup |

## Complete AppleTalk Error Codes

> **Verified:** Programming With AppleTalk

### ADSP Errors

| Code | Name | Meaning |
|------|------|---------|
| 0 | noErr | Success |
| -91 | ddpSktErr | Error opening DDP socket |
| -1273 | errOpenDenied | Connection request denied by remote |
| -1274 | errDSPQueueSize | Queue too small (< 100 bytes) |
| -1275 | errFwdReset | Read terminated by forward reset |
| -1276 | errAttention | Attention message > 570 bytes |
| -1277 | errOpening | Error during connection open |
| -1278 | errState | Wrong state for operation |
| -1279 | errAborted | Operation aborted |
| -1280 | errRefNum | Invalid connection reference |

### NBP Errors

| Code | Name | Meaning |
|------|------|---------|
| -1024 | nbpBuffOvr | Return buffer too small |
| -1025 | nbpNoConfirm | Cannot confirm name |
| -1026 | nbpConfDiff | Name confirmed at different socket |
| -1027 | nbpDuplicate | Name already exists on network |
| -1029 | nbpNISErr | Error opening Names Information Socket |

### ATP Errors

| Code | Name | Meaning |
|------|------|---------|
| -1024 | reqFailed | Retry count exceeded |
| -1096 | tooManyReqs | Too many concurrent requests |
| -1098 | tooManySockets | Too many sockets open |
| -1099 | badATPSocket | Invalid socket |
| -1100 | badBuffNum | Bad response buffer number |
| -1101 | noRelErr | No release received |
| -1102 | cbNotFound | Control block not found |
| -1104 | noDataArea | Out of memory |
| -1105 | reqAborted | Request cancelled |

## ADSP Timeout Behavior

> **Verified:** Programming With AppleTalk

- **Half-open connections:** ADSP closes after 2 minutes if contact cannot be reestablished
- **ocInterval:** Interval between open request retransmissions (in 10-tick units, ~1/6 second)
- **ocMaximum:** Maximum number of open request retransmissions

## CCB Key Fields

> **Verified:** Programming With AppleTalk

| Field | Purpose |
|-------|---------|
| state | Current connection state (sOpen, sClosed, etc.) |
| userFlags | Event flags (eClosed, eTearDown, eAttention, eFwdReset) |
| localCID | Local connection ID |
| remoteCID | Remote connection ID |
| remoteAddress | Address of remote connection end |
| sendSeq | Sequence number of first byte sent |
| sendWindow | Sequence number of last remote byte received |
| attnSendSeq | Sequence number of next attention to send |
| attnRecvSeq | Sequence number of next attention to receive |
