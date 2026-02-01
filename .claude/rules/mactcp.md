---
description: MacTCP-specific rules and patterns
paths:
  - "src/mactcp/**/*"
---

# MacTCP Rules

Rules for MacTCP networking code targeting 68k Macs with System 6.0.8 - 7.5.5.

## ASR (Asynchronous Status Routine)

The ASR runs at **interrupt level**. See `isr-safety.md` for general rules.

### ASR Restrictions

> **Verified:** MacTCP Programmer's Guide (Lines 2150-2156, 4226-4232):
> - **CANNOT** allocate or release memory
> - **CANNOT** make synchronous MacTCP driver calls
> - **CAN** issue additional asynchronous MacTCP driver calls

### ASR Register Context

> **Verified:** MacTCP Programmer's Guide (Lines 4265-4268, 1545-1548):

| Register | Contents |
|----------|----------|
| A0 | Stream pointer |
| A1 | Pointer to ICMP report (if event is TCPICMPReceived/UDPICMPReceived) |
| A2 | User data pointer |
| A5 | Already set to application globals |
| D0 (word) | Event code |
| D1 | Termination reason (for TCPTerminate only) |

### ASR Signature

```c
/* From MacTCP.h - use CALLBACK_API for pascal calling convention */
typedef CALLBACK_API(void, TCPNotifyProcPtr)(
    StreamPtr       tcpStream,      /* The stream this event is for */
    unsigned short  eventCode,      /* TCPDataArrival, TCPClosing, etc. */
    Ptr             userDataPtr,    /* Your context pointer from TCPCreate */
    unsigned short  terminReason,   /* Only valid for TCPTerminate */
    ICMPReport     *icmpMsg         /* Only valid for TCPICMPReceived */
);
```

### UPP Creation Required

```c
/* Create once at init */
TCPNotifyUPP g_tcp_upp = NewTCPNotifyUPP(my_tcp_asr);
UDPNotifyUPP g_udp_upp = NewUDPNotifyUPP(my_udp_asr);

/* Pass UPP to TCPCreate, not raw function pointer */
create_pb.notifyProc = g_tcp_upp;

/* Dispose at shutdown */
DisposeTCPNotifyUPP(g_tcp_upp);
DisposeUDPNotifyUPP(g_udp_upp);
```

### TCP ASR Event Codes

> **Verified:** MacTCP Programmer's Guide (Lines 4269-4312, 6471-6493)

| Code | Event | Meaning | Action |
|------|-------|---------|--------|
| 0 | `TCPClosing` | All data received and delivered, connection closing | Set `remote_close` flag |
| 1 | `TCPUrgent` | Urgent data outstanding | Set `urgent` flag, enter urgent mode |
| 2 | `TCPDataArrival` | Data arrived, no receive commands outstanding | Set `data_available` flag |
| 4 | `TCPULPTimeout` | No response from remote (only if configured to report) | Handle timeout |
| 5 | `TCPTerminate` | Connection no longer exists | Set `terminated` flag, save reason |
| 6 | `TCPICMPReceived` | ICMP message received (A1 points to report) | Set `icmp_received` flag |

> **Note:** Data arrival notification is given ONCE per batch - "TCP does not issue another data arrival notification until a receive command has been issued and completed" (Lines 4334-4338)

### UDP ASR Event Codes

> **Verified:** MacTCP Programmer's Guide (Lines 6464-6470)

| Code | Event | Meaning |
|------|-------|---------|
| 1 | `UDPDataArrival` | Datagram arrived, no UDPRead commands outstanding |
| 2 | `UDPICMPReceived` | ICMP received (A1 points to report) |

### TCP Termination Reasons

> **Verified:** MacTCP Programmer's Guide (Lines 4295-4310, 6497-6516)

When event code is `TCPTerminate`, D1 (terminReason) contains:

| Code | Reason | Meaning |
|------|--------|---------|
| 1 | Remote Abort | Remote TCP aborted the connection |
| 2 | Network Failure | Network failure occurred |
| 3 | Security/Precedence | Invalid security option or precedence level |
| 4 | ULP Timeout | ULP time-out expired with abort action |
| 5 | ULP Abort | User issued TCPAbort command |
| 6 | ULP Close | Connection closed gracefully |
| 7 | Service Failure | Unexpected connection initiation segment |

### ASR Pattern

```c
static pascal void tcp_asr(StreamPtr stream, unsigned short event,
                           Ptr userDataPtr, unsigned short terminReason,
                           ICMPReport *icmpMsg) {
    my_state *state = (my_state *)userDataPtr;

    switch (event) {
    case TCPDataArrival:
        state->flags.data_available = 1;
        break;
    case TCPClosing:
        state->flags.remote_close = 1;
        break;
    case TCPTerminate:
        state->flags.terminated = 1;
        state->term_reason = terminReason;
        break;
    case TCPUrgent:
        state->flags.urgent = 1;
        break;
    }
    /* NO other work - main loop handles it */
}
```

## TCPPassiveOpen is One-Shot

TCPPassiveOpen accepts ONE connection, then the stream is busy. To accept more:

1. Create dedicated "listener" stream
2. When connection arrives, transfer to "worker" stream
3. Re-issue TCPPassiveOpen on listener

### TCPPassiveOpen Timeout Behavior

> **Verified:** MacTCP Programmer's Guide (Lines 2215-2222, 2620-2637):
> - ULP timer NOT started until first connection segment arrives
> - Command time-out starts when command is issued
> - Minimum command time-out: 2 seconds; 0 means infinite
> - Minimum ULP time-out: 2 seconds; 0 means use default (2 minutes)
> - If remote IP/port are 0: accept from ANY remote TCP
> - If remote IP/port nonzero: accept ONLY from that specific remote

## Zero-Copy Receive (TCPNoCopyRcv)

> **Verified:** MacTCP Programmer's Guide (Lines 3064-3107, 3177-3180)

```c
rdsEntry rds[4];
err = TCPNoCopyRcv(stream, rds, 4, false, &iopb);

for (i = 0; i < 4 && rds[i].length > 0; i++) {
    process(rds[i].ptr, rds[i].length);
}

/* CRITICAL: Return buffer to MacTCP - RDS MUST be unmodified */
err = TCPRcvBfrReturn(stream, rds[0].ptr, &iopb);
```

> **CRITICAL (Line 2178):** "TCPBfrReturn must be called for every TCPNoCopyRcv that returns a nonzero amount of data"
> **CRITICAL (Line 3179):** "The RDS must be identical to the RDS given to the user when the TCPNoCopyRcv command is completed"

### TCPNoCopyRcv Completion Conditions

Command completes when:
1. Pushed data arrives
2. Urgent data is outstanding
3. Some reasonable period passes after nonpush/nonurgent data
4. RDS is full (more chunks than RDS can describe)
5. Data amount >= 25% of total receive buffering
6. Command time-out expires

## Complete Error Codes

> **Verified:** MacTCP Programmer's Guide (Lines 5939-6120)

| Code | Name | Meaning |
|------|------|---------|
| 0 | noErr | No error |
| -23004 | inProgress | IOPB still pending |
| -23005 | ipBadLapErr | Unable to initialize local network handler |
| -23006 | ipBadCnfgErr | Manually set address configured improperly |
| -23007 | ipNoCnfgErr | Configuration resource missing |
| -23008 | ipLoadErr | Not enough room in application heap |
| -23009 | ipBadAddr | Error getting address from server or address in use |
| -23010 | streamAlreadyOpen | Stream already using this receive buffer area |
| -23011 | invalidLength | Receive buffer too small (minimum 4096 for TCP) |
| -23012 | invalidBufPtr | Receive buffer pointer is 0 |
| -23014 | invalidRDS/invalidWDS | RDS refers to buffers not owned by user OR WDS is 0 |
| -23015 | openFailed | Connection came up halfway then failed |
| -23016 | commandTimeout | Command not completed in specified time |
| -23017 | duplicateSocket | Stream already using this port OR connection exists |
| -23018 | ipDontFragErr | Packet too large, Don't Fragment set |
| -23019 | ipDestDeadErr | Destination not responding to ARP |
| -23020 | ipNoFragMemErr | Insufficient buffers to fragment packet |
| -23021 | ipRouteErr | No gateway available for off-network destination |

### Additional Result Codes

| Name | Meaning |
|------|---------|
| connectionClosing | TCPClose already issued; no more data to send |
| connectionExists | Stream already has open connection |
| connectionDoesntExist | Stream has no open connection |
| insufficientResources | Already 64 TCP or UDP streams open |
| invalidStreamPtr | Specified stream not open |
| connectionTerminated | Connection went down; reason in ASR |

### DNS Resolver Errors

| Name | Meaning |
|------|---------|
| nameSyntaxErr | hostName had syntax error in dot notation |
| cacheFault | Name not in cache; resolver will query server |
| noResultProc | No result procedure for address translation |
| noNameServer | No name server found |
| authNameErr | Domain name does not exist |
| noAnsErr | No known name servers responding |

## System Limits

> **Verified:** MacTCP Programmer's Guide (Lines 926, 2144-2145, 1090, 2469)

| Resource | Limit | Notes |
|----------|-------|-------|
| Max streams | 64 total | System-wide, TCP + UDP combined |
| TCP buffer minimum | 4096 bytes | Line 2439 |
| TCP buffer recommended | 8192-16384 bytes | Up to 128KB useful (Lines 2444-2446) |
| UDP buffer minimum | 2048 bytes | Line 1057 |
| UDP datagram safe size | 576 bytes | MTU considerations (Line 1353) |

## Buffer Ownership

> **Verified:** MacTCP Programmer's Guide (Line 1055):
> "This buffer area for incoming datagrams belongs to the MacTCP driver as long as the UDP/TCP stream is open; it cannot be modified or relocated until Release is called"

## TCPClose Behavior

> **Verified:** MacTCP Programmer's Guide (Lines 2269-2287):
> - TCPClose = "I have no more data to send" (NOT a full shutdown)
> - Connection may remain open indefinitely after TCPClose
> - Data may still be **received** after TCPClose
> - Issue TCPRcv after TCPClose to ensure all data received
> - Use TCPAbort to break connection without delivery assurance

## csCode Values (Command Codes)

> **Verified:** MacTCP Programmer's Guide

### TCP Commands
| csCode | Command | Purpose |
|--------|---------|---------|
| 32 | TCPCreate | Create a TCP stream |
| 33 | TCPPassiveOpen | Listen for incoming connection |
| 36 | TCPActiveOpen | Initiate outgoing connection |
| 37 | TCPSend | Send data |
| 38 | TCPNoCopyRcv | Zero-copy receive |
| 40 | TCPBfrReturn | Return buffer from TCPNoCopyRcv |
| 41 | TCPRcv | Copy receive |
| 43 | TCPClose | Initiate graceful close |
| 44 | TCPAbort | Abort connection |
| 45 | TCPStatus | Get stream status |
| 47 | TCPRelease | Release stream |

### UDP Commands
| csCode | Command | Purpose |
|--------|---------|---------|
| 20 | UDPCreate | Create a UDP stream |
| 21 | UDPRead | Receive datagram |
| 22 | UDPBfrReturn | Return buffer |
| 23 | UDPWrite | Send datagram |
| 24 | UDPRelease | Release stream |
| 25 | UDPMaxMTUSize | Query MTU |

### IP Command
| csCode | Command | Purpose |
|--------|---------|---------|
| 15 | GetMyIPAddr | Get local IP address |

## WDS/RDS Structure Format

> **Verified:** MacTCP Programmer's Guide

### WDS (Write Data Structure) - for sending
```c
struct wdsEntry {
    unsigned short  length;    /* Length of buffer */
    Ptr             ptr;       /* Pointer to buffer */
};
/* Terminate with length = 0 */

/* UDP: Maximum 6 buffers, 0-8192 bytes total */
/* TCP: Unlimited buffers, 1-65535 bytes total */
```

### RDS (Read Data Structure) - for TCPNoCopyRcv
```c
struct rdsEntry {
    unsigned short  length;    /* Length of data */
    char           *ptr;       /* Pointer to data (in MacTCP buffers) */
};
/* Terminate with length = 0 */
/* MUST return unmodified to TCPBfrReturn */
```

## ICMP Report Structure

```c
typedef enum {
    netUnreach,           /* 0: Network unreachable */
    hostUnreach,          /* 1: Host unreachable */
    protocolUnreach,      /* 2: Protocol unreachable */
    portUnreach,          /* 3: Port unreachable */
    fragReqd,             /* 4: Fragmentation required but DF set */
    sourceRouteFailed,    /* 5: Source route failed */
    timeExceeded,         /* 6: TTL exceeded */
    missingOption,        /* 7: Required option missing */
    parmProblem           /* 8: Parameter problem */
} ICMPMsgType;

typedef struct {
    StreamPtr       streamPtr;
    ip_addr         localHost;
    ip_port         localPort;
    ip_addr         remoteHost;
    ip_port         remotePort;
    ICMPMsgType     reportType;
    unsigned short  optionalAddlInfo;
    unsigned long   optionalAddlInfoPtr;
} ICMPReport;
```
