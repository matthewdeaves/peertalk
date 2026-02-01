# Key Line References - Quick Lookup

When you know exactly which table or section you need, use these line references to jump directly to the content with Read(offset, limit).

## Inside Macintosh Volume VI (1991)

**Interrupt Safety Tables (Appendix B):**
| Table | Lines | Purpose |
|-------|-------|---------|
| Table B-1 | 223761-224228 | Routines that MOVE/PURGE memory - DO NOT call at interrupt |
| Table B-2 | 224216-224391 | Routines that don't move memory but still UNSAFE at interrupt |
| Table B-3 | 224396-224607 | **Routines SAFE to call at interrupt time** |

**Other key sections:**
- BlockMove discussion: Lines 162408-162411
- Gestalt interrupt safety: Lines 14164-14170

## Networking With Open Transport (v1.3)

**Interrupt Safety Tables (Appendix C):**
| Table | Lines | Purpose |
|-------|-------|---------|
| Table C-1 | 43052-43451 | **Functions callable at hardware interrupt time (all ISAs)** |
| Table C-2 | 43502-43740 | Functions callable at hardware interrupt time (native ISA only) |
| Table C-3 | 43748-44144 | Functions callable from deferred tasks (with async restrictions) |
| Table C-4 | (in appendix) | Functions that allocate memory |

**Other key sections:**
- Notifier restrictions: Pages 5793-5826
- Event codes: Lines 21076-21112
- Error codes (Table B-1): Lines 42307+
- kOTProviderWillClose: Lines 6002-6006, 21411-21428
- Reentrancy warning: Lines 5822-5826
- OTEnterInterrupt requirements: Lines 43041-43050

## MacTCP Programmer's Guide (1989)

**No formal table** - restrictions stated in text:
- ASR restrictions: Lines 2150-2156, 4226-4232
- Register preservation: Line 1510
- TCP event codes: Lines 4269-4312
- UDP event codes: Lines 6464-6470
- Termination reasons: Lines 4295-4310
- Error codes: Lines 5939-6120
- Buffer requirements: Lines 2438-2446

## Programming With AppleTalk (1991)

**No formal table** - restrictions stated in text (references IM III-V for memory-moving routines):
- Completion routine restrictions: Lines 1554-1558
- Interrupt level restrictions: Lines 1246-1247
- Callback rules: Lines 5924-5926
- userFlags clearing: Lines 5780-5782
- Register preservation: Lines 1675-1678, 11544
- ADSP errors: Lines 5896-7229
- NBP name limits: Line 8986

## Inside Macintosh Volume V (1986)

- Sound Manager callback restrictions: Lines 58110-58116
- InitCmd/FreeCmd timing: Lines 59860-59874
- Device Manager completion routines: Lines 61337-61340
- Slot interrupt polling routines: Lines 52238-52270
- Deferred Task Manager: Lines 56930-57221

## Usage

Instead of grepping the entire book, use Read with specific offset/limit:

```
# Example: Read Table B-3 directly
Read(file_path: "books/Inside_Macintosh_Volume_VI_1991.txt",
     offset: 224396, limit: 220)

# Example: Read MacTCP error codes section
Read(file_path: "books/MacTCP_Programmers_Guide_1989.txt",
     offset: 5939, limit: 181)
```

This is 100x faster than grepping and gives you the exact section you need.
