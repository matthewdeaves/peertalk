---
description: Open Transport-specific rules and patterns
paths:
  - "src/opentransport/**/*"
---

# Open Transport Rules

Rules for Open Transport networking code targeting PPC Macs with System 7.6.1+ or Mac OS 8/9.

## Notifiers

Notifiers run at **deferred task time or system task time**. See `isr-safety.md` for general rules.

> **Verified:** Networking With Open Transport (Pages 5793-5826):
> - MUST NOT move or purge memory
> - MUST NOT make synchronous calls to Open Transport
> - MUST NOT make synchronous Device Manager or File Manager calls (risk of deadlock)
> - MUST NOT perform time-consuming tasks
> - SHOULD assume code runs at deferred task level (Page 9248-9249)

### Notifier Signature

```c
typedef void (*OTNotifyProcPtr)(
    void        *contextPtr,    /* Your context from OTInstallNotifier */
    OTEventCode  code,          /* T_DATA, T_LISTEN, T_DISCONNECT, etc. */
    OTResult     result,        /* Result code for completion events */
    void        *cookie         /* Event-specific data */
);
```

### Functions Safe at Hardware Interrupt Time (Table C-1)

> **Verified:** Networking With Open Transport (Pages 43041-43495)

**Timing Functions (no OTEnterInterrupt needed):**
- `OTGetTimeStamp()` - Safe, unlike TickCount!
- `OTElapsedMilliseconds()` - Safe
- `OTElapsedMicroseconds()` - Safe

**Atomic Operations (all atomic):**
- `OTAtomicSetBit()`, `OTAtomicClearBit()`, `OTAtomicTestBit()`
- `OTAtomicAdd32()`, `OTAtomicAdd16()`, `OTAtomicAdd8()`

**Memory Functions (no OTEnterInterrupt needed):**
- `OTAllocMem()` - Safe but may return NULL
- `OTFreeMem()` - Safe

**Scheduling (OTEnterInterrupt needed for some):**
- `OTScheduleInterruptTask()` - No OTEnterInterrupt needed
- `OTScheduleDeferredTask()` - Needs OTEnterInterrupt
- `OTScheduleSystemTask()` - Needs OTEnterInterrupt

**String/List Functions:**
- `OTStrCopy()`, `OTStrCat()`, `OTStrLength()`, `OTStrEqual()`
- `OTEnqueue()`, `OTDequeue()` - Atomic
- `OTLIFOEnqueue()`, `OTLIFODequeue()`, `OTLIFOStealAndReverseList()` - All atomic

### OTAllocMem Warning

> **Verified:** Networking With Open Transport (Pages 9143-9148):
> "You can safely call the functions OTAllocMem and OTFreeMem from your notifier. **However, keep in mind that the memory allocated by OTAllocMem comes from the application's memory pool, which, due to Memory Manager constraints, can only be replenished at system task time. Therefore, if you allocate memory at hardware interrupt level or deferred task level, be prepared to handle a failure as a result of a temporarily depleted memory pool.**"

Always check for NULL and have a fallback (pre-allocated buffers).

### Reentrancy Warning

> **Verified:** Networking With Open Transport (Pages 5822-5826):
> "Open Transport might call a notification routine reentrantly. Open Transport attempts to queue calls to a notification routine to prevent reentrancy and to keep the processor stack from growing, but this behavior is not guaranteed. You should be prepared and write your notification routine defensively."

Use atomic operations for all flag manipulation.

> **T_MEMORYRELEASED Warning (Pages 21353-21355):** This event can be reentrant - handle defensively.

## Complete Event Codes

> **Verified:** Networking With Open Transport (Pages 21076-21112)

### Asynchronous Events

| Code | Event | Meaning |
|------|-------|---------|
| 0x0001 | `T_LISTEN` | Connection request arrived |
| 0x0002 | `T_CONNECT` | Passive peer accepted your connection |
| 0x0004 | `T_DATA` | Normal data arrived |
| 0x0008 | `T_EXDATA` | Expedited data arrived |
| 0x0010 | `T_DISCONNECT` | Connection torn down or rejected |
| 0x0020 | `T_ERROR` | Obsolete |
| 0x0040 | `T_UDERR` | UData send failed |
| 0x0080 | `T_ORDREL` | Remote orderly disconnect initiated |
| 0x0100 | `T_GODATA` | Flow control lifted for normal data |
| 0x0200 | `T_GOEXDATA` | Flow control lifted for expedited data |
| 0x0400 | `T_REQUEST` | Request arrived (transaction-based) |
| 0x0800 | `T_REPLY` | Response arrived (transaction-based) |
| 0x1000 | `T_PASSCON` | Connection passed to another endpoint |

### Completion Events

| Code | Event | Meaning |
|------|-------|---------|
| 0x20000001 | `T_BINDCOMPLETE` | Bind operation completed |
| 0x20000002 | `T_UNBINDCOMPLETE` | Unbind operation completed |
| 0x20000003 | `T_ACCEPTCOMPLETE` | Accept operation completed |
| 0x20000004 | `T_REPLYCOMPLETE` | Reply operation completed |
| 0x20000005 | `T_DISCONNECTCOMPLETE` | Disconnect operation completed |
| 0x20000006 | `T_OPTMGMTCOMPLETE` | Option management completed |
| 0x20000007 | `T_OPENCOMPLETE` | Open operation completed |
| 0x20000008 | `T_GETPROTADDRCOMPLETE` | Get protocol address completed |
| 0x20000009 | `T_RESOLVEADDRCOMPLETE` | Resolve address completed |
| 0x2000000A | `T_GETINFOCOMPLETE` | Get info completed |
| 0x2000000B | `T_SYNCCOMPLETE` | Sync completed |
| 0x2000000C | `T_MEMORYRELEASED` | Send buffer released (may be reentrant!) |
| 0x2000000D | `T_REGNAMECOMPLETE` | Register name completed |
| 0x2000000E | `T_DELNAMECOMPLETE` | Delete name completed |
| 0x2000000F | `T_LKUPNAMECOMPLETE` | Lookup name completed |
| 0x20000010 | `T_LKUPNAMERESULT` | Lookup name result |

### Provider Events

| Code | Event | Meaning |
|------|-------|---------|
| 0x23000001 | `kOTSyncIdleEvent` | Sync call waiting - call YieldToAnyThread |
| 0x23000002 | `kOTProviderIsDisconnected` | Provider disconnected from port |
| 0x24000001 | `kOTProviderIsReconnected` | Provider reconnected |
| 0x24000002 | `kOTProviderWillClose` | Provider closing at system task time |
| 0x24000003 | `kOTProviderIsClosed` | Provider already closed |

### kOTProviderWillClose Special Handling

> **Verified:** Networking With Open Transport (Pages 6002-6006, 21411-21428):
> "If you are closing a provider in response to a kOTProviderWillClose event, note that Open Transport issues this event **only at system task time**. Thus, you can set the endpoint to synchronous mode (from within the notifier function) and **call functions synchronously** to do whatever clean-up is necessary before you return from the notifier."

- Result parameter contains reason code (range -3280 through -3285)
- After return from notifier, provider closes
- Any calls other than `OTCloseProvider` will fail with `kOTOutStateErr`

### Notifier Pattern

```c
static pascal void tcp_notifier(void *context, OTEventCode code,
                                OTResult result, void *cookie) {
    my_endpoint *ep = (my_endpoint *)context;

    switch (code) {
    case T_DATA:
        OTAtomicSetBit(&ep->event_flags, kDataAvailableBit);
        break;
    case T_LISTEN:
        OTAtomicSetBit(&ep->event_flags, kListenPendingBit);
        break;
    case T_DISCONNECT:
        OTAtomicSetBit(&ep->event_flags, kDisconnectBit);
        break;
    case T_ORDREL:
        OTAtomicSetBit(&ep->event_flags, kOrderlyReleaseBit);
        break;
    case T_GODATA:
        OTAtomicSetBit(&ep->event_flags, kFlowControlLiftedBit);
        break;
    case T_BINDCOMPLETE:
    case T_CONNECTCOMPLETE:
        ep->async_result = result;
        OTAtomicSetBit(&ep->event_flags, kOperationCompleteBit);
        break;
    case kOTProviderWillClose:
        /* Special: CAN make sync calls here - system task time */
        OTSetSynchronous(ep->ref);
        /* Synchronous cleanup allowed */
        break;
    }
}
```

## Endpoint States

> **Verified:** Networking With Open Transport (Pages 6507-6764, Table 4-3)

| State | Meaning |
|-------|---------|
| `T_UNINIT` | Uninitialized (closed/not created) |
| `T_UNBND` | Unbound (initialized, not yet bound) |
| `T_IDLE` | Idle (bound, ready for use) |
| `T_OUTCON` | Outgoing connection request pending |
| `T_INCON` | Incoming connection request pending |
| `T_DATAXFER` | Data transfer (connection established) |
| `T_OUTREL` | Outgoing orderly release (can still read) |
| `T_INREL` | Incoming orderly release (can still send) |

### State Transitions

**Connectionless:**
```
T_UNINIT --Open--> T_UNBND --Bind--> T_IDLE --Unbind--> T_UNBND --Close--> T_UNINIT
```

**Connection-Oriented (Passive/Listener):**
```
T_IDLE --Listen--> T_INCON --Accept--> T_DATAXFER
```

**Connection-Oriented (Active/Initiator):**
```
T_IDLE --Connect--> T_OUTCON --RcvConnect--> T_DATAXFER
```

**Orderly Disconnect:**
```
T_DATAXFER --SndOrderlyDisconnect--> T_OUTREL --RcvOrderlyDisconnect--> T_IDLE
T_DATAXFER --RcvOrderlyDisconnect--> T_INREL --SndOrderlyDisconnect--> T_IDLE
```

> **Critical (Pages 6781-6787):** "Open Transport uses endpoint state information to manage endpoints. Consequently, it is crucial that you call functions in the right sequence and that you call functions to acknowledge receipt of data as well as of connection and disconnection requests."

## tilisten Module

> **Verified:** Networking With Open Transport (Pages 9450-9528)

For accepting multiple simultaneous connections without `kOTLookErr`:

```c
ep = OTOpenEndpoint(OTCreateConfiguration("tilisten,tcp"), 0, nil, &err);
```

**Problem Solved:** Multiple incoming connections can cause `kOTLookErr` when calling OTAccept if another T_LISTEN event arrives while processing the first one.

**Guarantee (Pages 9512-9515):** "When the tilisten module is installed in the stream, the OTAccept function will never fail because of a pending T_LISTEN event"

**Restrictions:**
- Only use for connection-oriented, listening endpoints
- NOT for hand-off endpoints or outgoing connections
- Requires Open Transport 1.1.1 or later

## Manual tilisten Pattern

For older OT versions or when not using tilisten module:

1. Bind with `qlen > 0` to enable listening
2. On `T_LISTEN`: call `OTListen()` to get pending connection info
3. Create new endpoint for the connection
4. Call `OTAccept()` with both listener and new endpoint
5. Handle `kOTLookErr` - check for additional T_LISTEN or T_DISCONNECT
6. Listener stays in listen state, ready for next connection

## kOTLookErr Handling

> **Verified:** Networking With Open Transport (Table 4-8, Page 103)

When a function returns `kOTLookErr`, an asynchronous event occurred. Call `OTLook()` to determine the event, handle it, then retry.

| Function | Pending Events That Cause kOTLookErr |
|----------|--------------------------------------|
| OTAccept, OTConnect | T_DISCONNECT, T_LISTEN |
| OTListen, OTRcvConnect, OTRcvOrderlyDisconnect, OTSndOrderlyDisconnect, OTSndDisconnect | T_DISCONNECT |
| OTRcv, OTRcvRequest, OTRcvReply, OTSnd, OTSndRequest, OTSndReply | T_GODATA, T_DISCONNECT, T_ORDREL |
| OTRcvUData, OTSndUData | T_UDERR |
| OTUnbind | T_LISTEN, T_DATA |

## Complete Error Codes

> **Verified:** Networking With Open Transport (Appendix B, Table B-1, Lines 42307+)

### Primary Errors (-3150 to -3180)

| Code | Name | Meaning |
|------|------|---------|
| 0 | kOTNoError | Success |
| -3150 | kOTBadAddressErr | Incorrect address format or illegal info |
| -3151 | kOTBadOptionErr | Incorrect option format |
| -3152 | kOTAccessErr | No permission for address/options |
| -3153 | kOTBadReferenceErr | Invalid provider reference |
| -3154 | kOTNoAddressErr | No address supplied or couldn't allocate |
| -3155 | kOTOutStateErr | Endpoint not in appropriate state |
| -3156 | kOTBadSequenceErr | Invalid sequence number |
| -3158 | kOTLookErr | Async event occurred - call OTLook() |
| -3159 | kOTBadDataErr | Data amount out of bounds |
| -3160 | kOTBufferOverflowErr | Buffer too small for data |
| -3161 | kOTFlowErr | Flow control prevents send/receive |
| -3162 | kOTNoDataErr | No data available (async/nonblocking) |
| -3163 | kOTNoDisconnectErr | No disconnect indication available |
| -3164 | kOTNoUDErrErr | No unit data error indication |
| -3165 | kOTBadFlagErr | Invalid flag value |
| -3166 | kOTNoReleaseErr | No orderly release indication |
| -3167 | kOTNotSupportedErr | Action not supported by endpoint |
| -3168 | kOTStateChangeErr | Transient state change in progress |
| -3169 | kOTStructureTypeErr | Invalid structType for OTAlloc/OTFree |
| -3170 | kOTBadNameErr | Invalid endpoint or host name |
| -3171 | kOTBadQLenErr | qlen=0 but trying to listen |
| -3172 | kOTAddressBusyErr | Address in use or unavailable |
| -3173 | kOTIndOutErr | Outstanding connection indications |
| -3174 | kOTProviderMismatchErr | Listener/acceptor endpoint mismatch |
| -3175 | kOTResQLenErr | Accepting endpoint has qlen > 0 |
| -3176 | kOTResAddressErr | Accepting endpoint bound to wrong address |
| -3177 | kOTQFullErr | Max outstanding indications reached |
| -3178 | kOTProtocolErr | Unspecified protocol error (fatal) |
| -3179 | kOTBadSyncErr | Sync call at interrupt time |
| -3180 | kOTCanceledErr | Provider closed or sync cancelled |

### Extended Errors (-3201 to -3285)

| Code | Name | Meaning |
|------|------|---------|
| -3201 | kOTNotFoundErr | Entity not found |
| -3204 | kOTENIOErr | I/O error |
| -3205 | kOTENXIOErr | No such device or address |
| -3208 | kOTEBADFErr | Invalid provider/stream reference |
| -3210 | kOTEAGAINErr | Would block, try again |
| -3211 | kOTENOMEMErr / kOTOutOfMemoryErr | Out of memory |
| -3215 | kOTEBUSYErr | Device busy |
| -3216 | kOTDuplicateFoundErr | Entity already exists |
| -3221 | kOTEINVALErr | Invalid operation or parameter |
| -3234 | kOTEWOULDBLOCKErr | Would block (nonblocking mode) |
| -3247 | kOTEADDRINUSEErr | Address in use |
| -3248 | kOTEADDRNOTAVAILErr | Address not available |
| -3249 | kOTENETDOWNErr | Network path unavailable |
| -3250 | kOTENETUNREACHErr | Network unreachable |
| -3253 | kOTECONNRESETErr | Connection reset |
| -3254 | kOTENOBUFSErr | No buffer space |
| -3259 | kOTETIMEDOUTErr | Operation timed out |
| -3260 | kOTECONNREFUSEDErr | Connection refused (port unreachable) |
| -3263 | kOTEHOSTDOWNErr | Host down |
| -3264 | kOTEHOSTUNREACHErr | Host unreachable |
| -3269 | kOTEPROTOErr | Catastrophic protocol error |
| -3270 | kOTETIMEErr | Ioctl timed out |
| -3271 | kOTENOSRErr | Out of system resources |

### Provider Close Reason Codes (-3279 to -3285)

| Code | Name | Meaning |
|------|------|---------|
| -3279 | kOTClientNotInittedErr | InitOpenTransport not called |
| -3280 | kOTPortHasDiedErr | Port unregistered |
| -3281 | kOTPortWasEjectedErr | Port ejected |
| -3282 | kOTBadConfigurationErr | TCP/IP improperly configured |
| -3283 | kOTConfigurationChangedErr | AppleTalk config changed |
| -3284 | kOTUserRequestedErr | User switched config in control panel |
| -3285 | kOTPortLostConnection | Port lost connection |
