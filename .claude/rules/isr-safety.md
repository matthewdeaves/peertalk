---
description: Interrupt-time safety rules for all Classic Mac platforms
---

# ISR Safety Rules

Rules for code that runs at interrupt time (MacTCP ASR, OT notifiers, ADSP callbacks).

## Universal Rules

**At interrupt time, you CAN:**
- Set volatile flags
- Increment counters
- Read pre-allocated buffers
- Use `pt_memcpy_isr()` (byte-copy, no Toolbox)
- Issue additional asynchronous network calls (MacTCP ASR only)

**At interrupt time, you CANNOT:**
- Allocate memory (malloc, NewPtr, NewHandle)
- Free memory (free, DisposePtr, DisposeHandle)
- Call TickCount() - NOT in Table B-3
- Use BlockMove/BlockMoveData - interrupt safety unclear (see note below)
- Do file I/O (FSRead, FSWrite, etc.)
- Make synchronous network calls
- Call most Toolbox routines
- Depend on handles to unlocked blocks being valid

> **Verified:** Inside Macintosh Volume V (Lines 58110-58116) states completion routines "must not make any calls to the Memory Manager, directly or indirectly, and can't depend on handles to unlocked blocks being valid."

## Register Preservation

All interrupt-time callbacks must preserve registers. The exact requirements vary by callback type:

| Callback Type | May Modify | Must Preserve |
|---------------|------------|---------------|
| MacTCP ASR | A0, A1, A2, D0, D1, D2 | D3-D7, A3-A7 |
| OT Notifier | (handled by system) | (handled by system) |
| ADSP Completion | A0, A1, D0, D1, D2 | D3-D7, A2-A6 |
| Sound Manager | A0, A1, D0-D2 | All others |

> **Verified:** MacTCP Programmer's Guide (Line 1510): "The values of all registers must be preserved except registers A0-A2 and D0-D2"
> **Verified:** Programming With AppleTalk (Lines 1675-1678, 11544): "registers D0 through D2 and registers A0 and A1 are saved by the interrupt handler for you" and "D3 through D7 and registers A2 through A6" must be preserved.

## Safe Alternatives

| Instead of... | Use... |
|---------------|--------|
| `memcpy()` | `pt_memcpy_isr()` |
| `TickCount()` | Set `timestamp = 0`, main loop timestamps |
| `malloc()` | Pre-allocated buffers in context struct |
| Sync network calls | Async calls with completion callbacks |
| `printf()` | Set flag, log from main loop |

## Inside Macintosh Table B-3 (Volume VI, Lines 224396-224606)

Only routines explicitly listed in Table B-3 are safe at interrupt time.

**Key safe routines from Table B-3:**
- `Gestalt` (with predefined selectors only)
- `NMInstall`, `NMRemove` (Notification Manager)
- `InsTime`, `InsXTime`, `RmvTime`, `PrimeTime` (Time Manager)
- `DeferUserFn` (Deferred Task Manager)
- `SleepQInstall`, `SleepQRemove` (Sleep Queue)
- `StripAddress`, `Translate24To32` (Address translation)
- `LockMemory`, `UnlockMemory`, `LockMemoryContiguous` (Virtual Memory)
- `GetPhysical`, `GetPageState` (Virtual Memory)
- Most S* Slot Manager routines
- Most PPC* routines (marked with + or ‡ for sync/async)

**NOT in Table B-3 (often assumed safe but aren't):**
- TickCount - NOT listed, can be disabled during interrupt
- BlockMove / BlockMoveData - NOT listed (see note below)
- NewPtr, NewHandle, DisposePtr, DisposeHandle - NOT listed
- Most Memory Manager routines
- Most Toolbox routines

> **BlockMove Note:** Inside Macintosh Volume VI (Lines 162408-162411) states BlockMove "does not cause blocks of memory to move or be purged, so you can safely call it in your doubleback procedure." However, it is NOT in Table B-3. The conservative approach is to avoid it. BlockMoveData has no documented interrupt-safety guarantee.

> **Gestalt Note:** Inside Macintosh Volume VI (Lines 14164-14170): "When passed one of the predefined selector codes, Gestalt does not move or purge memory and therefore may be called at any time, even at interrupt time." However, application-defined selectors may move/purge memory.

## Platform-Specific Timing

| Platform | Interrupt-Safe Timing |
|----------|----------------------|
| MacTCP ASR | None - use `timestamp = 0` |
| OT Notifier | `OTGetTimeStamp()`, `OTElapsedMilliseconds()`, `OTElapsedMicroseconds()` |
| ADSP Callback | None - use `timestamp = 0` |

> **Verified:** Networking With Open Transport Table C-1 (Lines 43052-43451) lists `OTGetTimeStamp()`, `OTElapsedMilliseconds()`, and `OTElapsedMicroseconds()` as safe at hardware interrupt time.

## Open Transport Interrupt-Safe Tables

> **Verified:** Networking With Open Transport Appendix C

| Table | Lines | Purpose |
|-------|-------|---------|
| C-1 | 43052-43451 | Functions callable at hardware interrupt time (all ISAs) |
| C-2 | 43502-43740 | Functions callable at hardware interrupt time (native ISA only) |
| C-3 | 43748-44144 | Functions callable from deferred tasks |

**Table C-1 key entries (safe at hardware interrupt time):**
- All `OTAtomic*` functions (OTAtomicSetBit, OTAtomicClearBit, OTAtomicAdd32, etc.)
- `OTAllocMem`, `OTFreeMem` (but may return NULL - check!)
- `OTGetTimeStamp`, `OTElapsedMilliseconds`, `OTElapsedMicroseconds`
- `OTScheduleInterruptTask` (no OTEnterInterrupt needed)
- `OTScheduleDeferredTask` (needs OTEnterInterrupt)
- `OTEnqueue`, `OTDequeue` (atomic)
- `OTLIFOEnqueue`, `OTLIFODequeue` (atomic)
- String functions: `OTStrCopy`, `OTStrCat`, `OTStrLength`, `OTStrEqual`
- `OTMemcpy`, `OTMemmove`, `OTMemset`, `OTMemzero`

**Table C-3 restrictions (deferred tasks):**
- Many functions require "asynchronous only" mode
- `OTAccept`, `OTBind`, `OTConnect` - asynchronous only
- `OTRcv`, `OTSnd`, `OTListen`, `OTLook` - asynchronous only
- Some require "foreground task must be calling SystemTask"

## The "Set Flag, Process Later" Pattern

```c
/* In callback - just set flags */
static pascal void my_callback(...) {
    state->flags.event_occurred = 1;  /* OK: atomic flag set */
    /* NO processing here */
}

/* In main loop - do the work */
void poll(void) {
    if (state->flags.event_occurred) {
        state->flags.event_occurred = 0;
        handle_event();  /* Safe to do anything here */
    }
}
```

## Memory Allocation Pattern

> **Verified:** Inside Macintosh Volume V (Lines 59860-59866): "During an interrupt your synthesizer or modifier should initialize any global data... It is important to note that any storage being allocated by the synthesizer or modifier should be locked, since the memory manager cannot be called during an interrupt."

**Pattern:** Allocate and lock memory at initialization time, use it at interrupt time:

```c
/* At init time (NOT interrupt) */
my_buffer = NewPtrClear(BUFFER_SIZE);
/* Lock if using handles: HLock(my_handle); */

/* At interrupt time - use pre-allocated buffer */
pt_memcpy_isr(my_buffer, data, len);  /* Safe */
```

## Deferred Task Manager

> **Verified:** Inside Macintosh Volume V (Lines 56930-57221)

For lengthy interrupt work, defer to system task time using DTInstall:

```c
/* Structure for deferred task */
typedef struct {
    QElemPtr    qLink;      /* Next queue entry */
    short       qType;      /* Must be dtQType */
    short       dtFlags;    /* Reserved */
    ProcPtr     dtAddr;     /* Pointer to task routine */
    long        dtParm;     /* Parameter (loaded into A1) */
    long        dtReserved; /* Reserved (0) */
} DeferredTask;

/* In interrupt handler - queue work for later */
DTInstall(&myDeferredTask);

/* Task executes at priority 0 (all interrupts enabled) */
```

**Benefits:**
- Slot interrupts on Mac II generate level-2 interrupts
- Without deferral, other level-2 (sound) and level-1 interrupts are blocked
- DTInstall queues task; system executes with interrupts enabled

## Table B-1: Routines That Move/Purge Memory (UNSAFE)

> **Verified:** Inside Macintosh Volume VI (Lines 223761-224228)

These routines may move or purge memory - **DO NOT call from interrupts, VBL tasks, or completion routines:**

**Memory/Resource:** NewHandle, NewPtr, DisposeHandle, DisposePtr, SetHandleSize, SetPtrSize, ReallocateHandle, RecoverHandle, HLock, HUnlock, HPurge, HNoPurge, MoveHHi, TempNewHandle, TempMaxMem, OpenResFile, CloseResFile, Get1Resource, GetResource, ReleaseResource, WriteResource, CreateResFile, FSpCreateResFile, FSpOpenResFile, ReadPartialResource, WritePartialResource, SetResourceSize

**Apple Events:** AECreateAppleEvent, AECreateDesc, AECreateList, AEDisposeDesc, AEDuplicateDesc, AESend, AEProcessAppleEvent (all AE* routines)

**Graphics:** NewGWorld, UpdateGWorld, DisposeGWorld, NewPalette, DisposePalette, CopyDeepMask, BitMapToRegion, OpenCPicture, GetPictInfo, NewPictInfo

**Edition Manager:** All edition routines (OpenEdition, CloseEdition, ReadEdition, WriteEdition, etc.)

**File/Alias:** NewAlias, NewAliasMinimal, ResolveAlias, ResolveAliasFile, MatchAlias, UpdateAlias

**Sound:** SndNewChannel, SndPlay, SndRecord, SndDoCommand, SndAddModifier

**Process:** LaunchApplication, LaunchDeskAccessory, WaitNextEvent

## Table B-2: Routines That Don't Move Memory But Still UNSAFE

> **Verified:** Inside Macintosh Volume VI (Lines 224216-224391)

These don't move memory but are NOT reentrant or examine movable memory:

**GWorld:** DisposeGWorld, GetGWorld, SetGWorld, GetGWorldDevice, LockPixels, UnlockPixels, GetPixBaseAddr, AllowPurgePixels, NoPurgePixels

**File System:** FSMakeFSSpec, FSpCreate, FSpDelete, FSpRename, FSpGetFInfo, FSpSetFInfo, FSpCatMove, FSpDirCreate, FSpExchangeFiles, CloseWD, HCreate, HDelete, HGetFInfo, HSetFInfo, HRename

**Palette/Color:** GetEntryColor, SetEntryColor, GetEntryUsage, SetEntryUsage, CTabChanged, PixPatChanged, GDeviceChanged

**Process:** GetCurrentProcess, SetFrontProcess, GetProcessSerialNumberFromPortName, GetPortNameFromProcessSerialNumber

**AppleTalk:** LAPAddATQ, LAPRmvATQ, GetLocalZones, GetMyZone, GetZoneList (sync versions)

**AE:** AECountItems, AEGetInteractionAllowed, AESuspendTheCurrentEvent

**Memory:** AllocContig, HoldMemory (some operations)
