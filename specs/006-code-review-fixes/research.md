# Research: Code Review Fixes

## R1: 68k Interrupt Disable Pattern

**Decision**: Use GCC inline assembly (`__asm__ __volatile__`) to save SR, set interrupt mask to level 7, and restore SR.

**Rationale**: No Toolbox call exists for disabling interrupts. Classic Mac apps run in supervisor mode (Inside Macintosh Vol I/II/III lines 8634-8641), so SR manipulation is safe. Inside Macintosh Vol V (lines 1538-1546) warns against it for forward compatibility, but this code targets 68k Classic Mac only. Research entry R27 in specs/001 already recommends this approach.

**Verified**: Compiled with `m68k-apple-macos-gcc -std=c89 -Wall -Wextra`. Disassembly confirms correct instructions: `movew %sr,%d0`, `oriw #0x0700,%sr`, `movew %d0,%sr`.

**Implementation**:
```c
static short pt_disable_interrupts(void)
{
    short old_sr;
    __asm__ __volatile__(
        "move.w %%sr, %0\n\t"
        "ori.w #0x0700, %%sr"
        : "=d"(old_sr) : : "cc"
    );
    return old_sr;
}

static void pt_restore_interrupts(short old_sr)
{
    __asm__ __volatile__(
        "move.w %0, %%sr"
        : : "d"(old_sr) : "cc"
    );
}
```

**Alternatives considered**:
- `tas` instruction: operates on bytes and sets bit 7 only, not useful for clearing a flags word
- BlockMove: NOT in Table B-3, not interrupt-safe
- Disable/enable via Toolbox: no such API exists

## R2: OTAtomic* Functions for OT Backend

**Decision**: Use OTAtomicSetBit in notifiers and OTAtomicClearBit (which returns previous state) in poll loop. Change flags from `volatile unsigned long` to `volatile UInt8` with bit-index constants.

**Rationale**: OTAtomicSetBit, OTAtomicClearBit, and OTAtomicTestBit are all in Table C-1 (Networking With Open Transport lines 43123-43144), safe at hardware interrupt time without needing OTEnterInterrupt. They operate on `UInt8*` with bit indices 0-7.

**Function signatures** (from `<OpenTransport.h>`):
```c
Boolean OTAtomicSetBit(UInt8* bytePtr, size_t bitToSet);
Boolean OTAtomicClearBit(UInt8* bytePtr, size_t bitToClear);
Boolean OTAtomicTestBit(UInt8* bytePtr, size_t bitToTest);
```

Each returns the **previous** state of the bit. This makes OTAtomicClearBit perfect for atomic test-and-clear in the poll loop.

**New flag scheme**:
```c
#define EVT_BIT_DATA        0
#define EVT_BIT_DISCONNECT  1
#define EVT_BIT_ORDREL      2
#define EVT_BIT_CONNECT     3
#define EVT_BIT_LISTEN      4
#define EVT_BIT_PASSCON     5
#define EVT_BIT_GODATA      6
```

**Notifier**: `OTAtomicSetBit(&slot->flags, EVT_BIT_DATA);`
**Poll**: `had_data = OTAtomicClearBit(&slot->flags, EVT_BIT_DATA);`

**Alternatives considered**:
- OTCompareAndSwap8 loop for snapshot-and-clear: more complex, no benefit since we process each flag individually anyway
- Keep unsigned long flags with OTCompareAndSwap32: requires 4-byte alignment, more complex
- OTAtomicAdd32: not applicable to bitmask operations

## R3: MacTCP Init Cleanup Sequence

**Decision**: Use goto-based cleanup with labels matching resource creation order. Model on existing `mactcp_shutdown` function.

**Rationale**: MacTCP Programmer's Guide confirms:
- TCPRelease (line 4012): closes stream, implicitly aborts any open connection, returns receive buffer
- UDPRelease (line 1422): closes stream, terminates outstanding commands. Must wait for pending UDPWrite to complete first (not an issue during init — no writes pending).
- UPPs must be disposed after all streams referencing them are released.

**Correct cleanup order for init failure**:
1. TCPRelease each created TCP stream
2. DisposePtr each TCP receive buffer
3. UDPRelease each created UDP stream
4. DisposePtr each UDP receive buffer
5. DisposeTCPNotifyUPP / DisposeUDPNotifyUPP
6. (Driver close not needed — shared resource)

**Note**: During init, no connections or async operations are active, so TCPRelease/UDPRelease can be called directly without TCPAbort or waiting for pending writes.

## R4: OT Init Cleanup Sequence

**Decision**: Use goto-based cleanup with labels matching resource creation order. Model on existing `ot_shutdown` function.

**Rationale**: Networking With Open Transport confirms:
- OTCloseProvider (line 21621): closes endpoint, disposes memory, cancels pending async operations
- UPPs must be disposed after all endpoints referencing them are closed
- CloseOpenTransport should be called last
- CloseOpenTransport "attempts to close [endpoints] on behalf of the client" (line 5857) as a safety net, but explicit cleanup is recommended (line 4110)

**Correct cleanup order for init failure**:
1. OTCloseProvider on each created TCP data endpoint
2. OTCloseProvider on listener endpoint (if created)
3. OTCloseProvider on each created UDP endpoint
4. DisposeOTNotifyUPP for listener, TCP, and UDP UPPs
5. CloseOpenTransport last

**Note**: During init, no async operations are pending (endpoints are opened synchronously), so OTCloseProvider is safe to call immediately. No T_MEMORYRELEASED events to worry about during init.

## R5: Reassembly Per-Chunk Bounds Check

**Decision**: Replace aggregate total_size admission check with per-chunk offset + payload bounds check.

**Rationale**: The current calculation `total_size = total * first_chunk_payload` overestimates because the last chunk is smaller. The correct approach is to check each individual chunk: `offset + chunk_payload <= reassembly_buf_size`. This is already partially done at the memcpy site but the aggregate check rejects valid messages before they get there.

**Alternatives considered**:
- Track actual total from sender: would require a wire protocol change (adding total_bytes field to chunk header)
- Use last chunk to calculate exact total: only possible when last chunk arrives, but the admission check runs on the first chunk
