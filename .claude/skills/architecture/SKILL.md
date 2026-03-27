---
name: architecture
description: |
  Fact-check and update ARCHITECTURE.md against the actual codebase.
  Reads source files, counts lines and functions, compares against
  the existing diagrams, and fixes any drift. Only changes what's
  wrong — does not regenerate from scratch.
allowed-tools: Read, Write, Edit, Glob, Grep, Bash
---

# Architecture Diagram Fact-Checker

Fact-check `ARCHITECTURE.md` against the actual codebase and fix any drift. Do NOT regenerate from scratch — read the existing file, compare each fact against the code, and use Edit to fix only what's wrong.

## Live Codebase Snapshot

Compare these values against what ARCHITECTURE.md currently says:

**Public API function count:**
!`grep -cE '^\w.+PT_\w+\(' include/peertalk.h 2>/dev/null || echo "?"`

**SDK line counts (per file):**
!`wc -l src/core/*.c src/core/*.h src/platform/*/*.c include/peertalk.h 2>/dev/null`

**Test line counts:**
!`wc -l tests/test_*.c tests/test_common.h tests/status_window.c tests/status_window.h 2>/dev/null`

**Platform backends:**
!`ls -d src/platform/*/ 2>/dev/null | sed 's|.*/||;s|/||'`

**Test apps:**
!`ls tests/test_*.c 2>/dev/null | sed 's|.*/test_||;s|\.c||' | grep -v common`

**Port numbers:**
!`grep -E 'PT_DISCOVERY_PORT|PT_TCP_PORT|PT_UDP_MSG_PORT' src/core/pt_internal.h 2>/dev/null`

## Required Diagrams

ARCHITECTURE.md MUST contain all of the following sections. If any are missing, add them.

### Structure Diagrams (static — what exists)

1. **System Context** (`graph TB`) — Developer, PeerTalk SDK, LAN, clog
2. **Containers** (`graph TB`) — SDK boundary with API, Core, each backend + LOC
3. **Core Components** (`graph LR`) — each core .c file with LOC and role
4. **Platform Backends** (`graph TB`) — one subgraph per backend, 2-3 subsystems each
5. **Deployment** (`graph LR`) — one subgraph per machine with CPU/RAM, full mesh
6. **Wire Protocol** (`graph LR`) — each frame format with port and header size
7. **Test Suite** (`graph LR`) — each test app with LOC and purpose

### Behavioral Diagrams (dynamic — how things flow)

8. **Discovery and Connection Sequence** (`sequenceDiagram`) — The full flow:
   - Peer A broadcasts discovery on UDP :7353 (magic PTLK + name)
   - Peer B receives, creates peer record, fires on_discovered callback
   - App calls PT_Connect → TCP connect to :7354
   - Meanwhile Peer B also discovers A and calls PT_Connect
   - Tiebreaker: higher IP cancels outgoing, accepts incoming
   - TCP handshake completes → on_connected fires on both sides
   - Read the actual tiebreaker logic from `pt_handle_incoming_connection` in pt_core.c to get the IP comparison direction right

9. **Message Send/Receive Sequence** (`sequenceDiagram`) — The pipeline:
   - App calls PT_Send(ctx, peer, type, data, len)
   - Core checks message_types[type] → PT_FAST or PT_RELIABLE
   - PT_FAST path: build 3B UDP header, call platform udp_send to :7355
   - PT_RELIABLE path: build 4B TCP header, call platform tcp_send to :7354
   - If message > send buffer: chunk with 8B headers, send each chunk
   - Receiver: platform poll receives data, calls pt_messaging_process_tcp_data or process_udp_data
   - Core parses frame, dispatches to registered callback
   - Read pt_messaging.c for the exact branching logic

10. **Init Memory Layout** (`graph LR`) — How the single-block allocator works:
    - PT_Init calls pt_memory_calculate_size
    - On Classic Mac: FreeMem() × 75%, subtract platform overhead, divide by per-peer cost
    - One NewPtrClear/malloc for the entire block
    - Block carved into: context overhead → peer array → per-peer buffers (tcp_recv + tcp_send + udp + reassembly)
    - Read pt_memory.c for the exact layout and sizing formula

11. **Chunking and Reassembly Sequence** (`sequenceDiagram`) — Large message flow:
    - App calls PT_Send with data larger than tcp_send_size
    - Core calculates total_chunks = ceil(len / max_chunk_payload)
    - For each chunk: build 8B header (base 4B + 2B seq + 2B total) + payload slice
    - Send each chunk via platform tcp_send
    - Receiver: pt_messaging_process_tcp_data sees CHUNK_FLAG
    - First chunk (seq=0): record stride and total, start reassembly timer (5s)
    - Each chunk: copy to reassembly_buf at offset = seq × stride
    - Last chunk: deliver complete message to callback
    - Read pt_messaging.c send path and receive path for exact details

12. **Poll Cycle** (`graph TB` or `sequenceDiagram`) — One PT_Poll iteration:
    - App calls PT_Poll(ctx)
    - Core updates current_time
    - Core checks discovery broadcast timer (every 2s)
    - Core calls platform poll()
    - Platform checks each socket/stream/endpoint for events
    - For each event: dispatch to core (discovery_receive, process_tcp_data, process_udp_data, handle_disconnect)
    - Core checks timeouts: discovery (15s), TCP inactivity (60s), connect (15s), reassembly (5s)
    - Read pt_core.c PT_Poll and the platform poll functions for the exact sequence

## Execution Steps

### Step 1: Read ARCHITECTURE.md

Read the existing `ARCHITECTURE.md` and extract every factual claim:
- Function counts, LOC counts, file names, port numbers, machine specs
- Check that all 12 diagram sections exist

### Step 2: Gather Current Facts from Code

For each claim found in Step 1, verify against the codebase:

1. **Function count** — `grep -cE '^\w.+PT_\w+\(' include/peertalk.h`
2. **Per-file LOC** — `wc -l` on each file mentioned in diagrams
3. **Layer totals** — sum the per-file counts
4. **File existence** — `ls` each file referenced
5. **Platform backends** — `ls -d src/platform/*/`
6. **Test apps** — `ls tests/test_*.c`
7. **Port numbers** — grep `pt_internal.h`
8. **Wire protocol** — grep for header sizes, version, magic bytes
9. **Machine specs** — read `.claude/mcp-servers/classic-mac-hardware/machines.json` or CLAUDE.md
10. **Behavioral accuracy** — read the actual functions referenced in sequence diagrams to verify the flow is correct

### Step 3: Compare and Fix

For each discrepancy:
1. Log: `DRIFT: <section> says <old> but code shows <new>`
2. Use Edit to fix only what's wrong
3. If a section is missing entirely, add it

### Step 4: Report

```
=== Architecture Fact-Check ===
Checked: <N> facts
Correct: <N>
Fixed: <N> (list each fix)
Sections added: <N> (list each)
Sections missing: <N> (list each)
```

## Diagram Format Rules

**Structure diagrams:** Standard Mermaid flowcharts (`graph TB` or `graph LR`).

**Behavioral diagrams:** Mermaid `sequenceDiagram` for flows between components, or `graph` for single-component internal flows.

Do NOT use Mermaid C4 diagram types (`C4Context`, `C4Container`, etc.).

**Style convention (flowcharts only — sequence diagrams don't use these):**
- Primary elements: `fill:#1168bd,stroke:#0b4884,color:#fff`
- Components: `fill:#438dd5,stroke:#2e6295,color:#fff`
- People: `fill:#08427b,stroke:#052e56,color:#fff`
- External systems: `fill:#999,stroke:#666,color:#fff`

**Label convention:** Keep labels short. No sentences in boxes. Details go in sequence diagram notes or arrow labels.
