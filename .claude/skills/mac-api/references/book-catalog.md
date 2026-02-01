# Reference Book Catalog

Complete descriptions of all authoritative source books available in the `books/` directory.

These are the ground truth - always verify against them.

## Primary Books

### MacTCP Programmer's Guide (1989)
**File:** `books/MacTCP_Programmers_Guide_1989.txt`

**Contents:** MacTCP API (primary reference)

**Key Sections:**
- ASR (Asynchronous Service Routine) rules and restrictions
- Parameter blocks for TCP/UDP operations
- Error codes and their meanings
- Stream lifecycle and buffer management
- TCPCreate, TCPPassiveOpen, TCPActiveOpen, TCPSend, TCPRcv
- TCPNoCopyRcv and TCPBfrReturn buffer handling
- UDP stream creation and datagram handling
- DNR (Domain Name Resolver) if referenced

**Critical for:**
- MacTCP ASR interrupt-safety rules
- Register preservation requirements
- Event codes and termination reasons
- Buffer sizing calculations

---

### Networking With Open Transport v1.3
**File:** `books/NetworkingOpenTransport.txt`

**Contents:** Open Transport API (primary reference)

**Key Sections:**
- Notifier callback rules and restrictions (Pages 5793-5826)
- Endpoint lifecycle and state management
- OTOpenEndpoint, OTBind, OTConnect, OTListen, OTAccept patterns
- Event handling (T_DATA, T_LISTEN, T_DISCONNECT, T_ORDREL)
- OTSnd, OTRcv, OTSndUData, OTRcvUData usage
- tilisten pattern for accepting multiple connections
- Atomic operations (OTAtomicSetBit, OTAtomicAdd, etc.)
- Flow control (T_GODATA) handling
- Error codes and kOTXXX constants
- **Table C-1:** Functions callable at hardware interrupt time (Lines 43052-43451)
- **Table C-2:** Functions callable at hardware interrupt (native ISA only) (Lines 43502-43740)
- **Table C-3:** Functions callable from deferred tasks (Lines 43748-44144)

**Critical for:**
- Open Transport notifier interrupt-safety
- Three execution levels (hardware interrupt, deferred task, system task)
- kOTProviderWillClose special handling
- OTScheduleDeferredTask usage
- OTGetTimeStamp/OTElapsedMilliseconds (interrupt-safe timing)

---

### Programming With AppleTalk (1991)
**File:** `books/Programming_With_AppleTalk_1991.txt`

**Contents:** AppleTalk, ADSP, NBP protocols

**Key Sections:**
- NBP (Name Binding Protocol) registration and lookup (Chapter 5)
- ADSP (AppleTalk Data Stream Protocol) connections (Chapter 8)
- ioCompletion and userRoutine callback rules (Chapter 3)
- CCB (Connection Control Block) structure and userFlags
- DSPParamBlock usage for async operations
- Zone and network number handling
- LocalTalk vs EtherTalk considerations
- .MPP and .DSP driver usage
- AppleTalk error codes

**Critical for:**
- ADSP completion routine interrupt-safety
- userFlags clearing requirement
- Register preservation in callbacks (D3-D7, A2-A6)
- Parameter block integrity rules

---

### Inside Macintosh Volume VI (1991)
**File:** `books/Inside_Macintosh_Volume_VI_1991.txt`

**Contents:** System 7.0 services and compatibility

**Key Sections:**
- **Appendix B:** Memory and interrupt-safety tables
  - **Table B-1:** Routines that move/purge memory (Lines 223761-224228)
  - **Table B-2:** Routines that don't move memory but still unsafe (Lines 224216-224391)
  - **Table B-3:** Routines safe to call at interrupt time (Lines 224396-224607)
- Memory Manager: NewPtr, NewHandle, DisposePtr, FreeMem, MaxBlock
- BlockMove vs BlockMoveData differences (Lines 162408-162411)
- Gestalt selectors for capability detection (Lines 14164-14170)
- Resource Manager usage
- 68k vs PPC differences
- System version requirements

**Critical for:**
- Universal interrupt-safety rules (Table B-3)
- What functions to NEVER call at interrupt time (Tables B-1, B-2)
- Gestalt interrupt-safety with predefined selectors
- Verification that TickCount() is NOT in Table B-3

---

### Inside Macintosh Volume V (1986)
**File:** `books/Inside_Macintosh_Volume_V_1986.txt`

**Contents:** Device Manager, Sound Manager, slots

**Key Sections:**
- Sound Manager callback restrictions (Lines 58110-58116)
- InitCmd/FreeCmd timing for memory allocation (Lines 59860-59874)
- Device Manager completion routines (Lines 61337-61340)
- Slot interrupt polling routines (Lines 52238-52270)
- Deferred Task Manager (Lines 56930-57221)
- Memory allocation in interrupt contexts
- VBL task restrictions

**Critical for:**
- Completion routine memory allocation rules
- Quote: "must not make any calls to the Memory Manager, directly or indirectly"
- Deferred Task Manager patterns (DTInstall)
- Handle locking requirements for interrupt-time storage

---

### Inside Macintosh Volume IV (1986)
**File:** `books/Inside_Macintosh_Volume_IV_1986.txt`

**Contents:** Color QuickDraw, SCSI Manager

**Key Sections:**
- Resource Manager
- SCSI Manager
- Additional memory-moving trap documentation

---

### Inside Macintosh Volumes I, II, III (1985)
**File:** `books/Inside Macintosh Volume I, II, III - 1985.txt`

**Contents:** Original Inside Macintosh foundational volumes

**Key Sections:**
- Memory Manager basics
- Toolbox fundamentals
- Original trap documentation
- Memory-moving routines (referenced by AppleTalk book)

---

## Secondary References

### MacTCP Programming Examples
**File:** `books/MacTCP_programming.txt`

**Contents:** Additional MacTCP code samples and patterns

**Use for:** Supplemental examples beyond the Programmer's Guide

---

### Inside Macintosh - Text
**File:** `books/Inside Macintosh - Text.txt`

**Contents:** Searchable text index from all Inside Macintosh volumes

**Use for:** Full-text search across all IM volumes when you need broad coverage

---

### Vintage Mac TCP/IP Reference
**File:** `books/VintageMacTCPIP_Reference.md`

**Contents:** System version compatibility and requirements

**Use for:**
- System version requirements for MacTCP vs Open Transport
- Hardware compatibility
- Network stack availability by OS version

---

## Search Priority

When researching a question:

1. **Check the primary book** for the topic area (MacTCP Guide for MacTCP, etc.)
2. **Check Inside Macintosh VI** for system-level and interrupt-safety rules
3. **Check Inside Macintosh V** for completion routine and memory allocation patterns
4. **Check secondary references** only if primary sources don't have the answer
5. **Use the text index** as a last resort for broad searches

This ordering ensures you get the most authoritative and specific information first.
