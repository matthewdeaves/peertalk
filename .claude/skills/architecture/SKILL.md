---
name: architecture
description: |
  Fact-check and update ARCHITECTURE.md against the actual codebase.
  Reads source files, counts lines and functions, compares against
  the existing diagrams, and fixes any drift. Only changes what's
  wrong — does not regenerate from scratch.
disable-model-invocation: true
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

## Execution Steps

### Step 1: Read ARCHITECTURE.md

Read the existing `ARCHITECTURE.md` and extract every factual claim:
- Function counts (e.g., "22 functions")
- LOC counts per file and per layer (e.g., "669 LOC")
- Total LOC counts (e.g., "4,410 LOC")
- File names referenced in diagrams
- Platform backend names
- Test app names and descriptions
- Port numbers and wire protocol details
- Machine specs in deployment diagram

### Step 2: Gather Current Facts from Code

For each claim found in Step 1, verify against the codebase:

1. **Function count** — `grep -cE '^\w.+PT_\w+\(' include/peertalk.h`
2. **Per-file LOC** — `wc -l` on each file mentioned in diagrams
3. **Layer totals** — sum the per-file counts
4. **File existence** — `ls` each file referenced. Flag missing files.
5. **Platform backends** — `ls -d src/platform/*/` — flag any missing from diagrams or extra in diagrams
6. **Test apps** — `ls tests/test_*.c` — flag any missing or extra
7. **Port numbers** — grep `pt_internal.h` for port defines
8. **Wire protocol** — grep for header sizes, version, magic bytes
9. **Machine specs** — read `.claude/mcp-servers/classic-mac-hardware/machines.json` or CLAUDE.md

### Step 3: Compare and Fix

For each discrepancy found:
1. Log: `DRIFT: <section> says <old> but code shows <new>`
2. Use the Edit tool to fix the specific value in ARCHITECTURE.md
3. Do NOT rewrite entire sections — only change the wrong values

If a new file/backend/test app exists that isn't in the diagrams, add it to the appropriate section using Edit.

If a file/backend/test app was removed, remove it from the appropriate section.

### Step 4: Report

Print a summary:
```
=== Architecture Fact-Check ===
Checked: <N> facts
Correct: <N>
Fixed: <N> (list each fix)
New items added: <N> (list each)
Removed items: <N> (list each)
```

## Diagram Format Rules

All diagrams use standard Mermaid flowcharts (`graph TB` or `graph LR`). Do NOT use Mermaid C4 diagram types (`C4Context`, `C4Container`, etc.) — they render poorly on GitHub.

**Style convention:**
- Primary elements: `fill:#1168bd,stroke:#0b4884,color:#fff`
- Components: `fill:#438dd5,stroke:#2e6295,color:#fff`
- People: `fill:#08427b,stroke:#052e56,color:#fff`
- External systems: `fill:#999,stroke:#666,color:#fff`

**Label convention:** Keep labels short — name on first line, LOC on second, brief role in italics on third. No sentences.
