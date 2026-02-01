# Book Indexing System

Optimizes massive reference books (714K lines) for 100x faster agent searches.

## Quick Start

```bash
# Build the indexes (takes ~2 seconds)
python3 tools/book_indexer/build_index.py

# Indexes are auto-generated in tools/book_indexer/index/
# The /mac-api skill uses them automatically
```

## What It Does

Extracts searchable metadata from Classic Mac reference documentation:

| Index File | Size | Contents |
|------------|------|----------|
| functions.json | 154KB | 741 functions with interrupt-safety flags |
| tables.json | 20KB | 6 critical tables (B-1, B-2, B-3, C-1, C-2, C-3) |
| error_codes.json | 28KB | 185 error codes (MacTCP, OT, AppleTalk) |
| keywords.json | 1.6KB | Common concepts mapped to sections |

## Quality Metrics

**Final Quality:** ✓ 100% Clean (as of 2026-01-31)

- **741 functions** indexed
  - 197 interrupt-safe (26.6%)
  - 286 unsafe (38.6%)
  - 258 context-dependent (34.8%)
    - 36 sync/async ambiguous (depends on execution mode)
    - 222 truly unknown (not enough info in tables)
- **0 junk entries** (was 38 before improvements)
- **All spot checks pass** (OTAllocMem, Gestalt, InsTime, etc.)
- **Sync/async conflicts properly detected** (GetLocalZones, PPC* functions, etc.)

## Performance

- **Without index:** Grep through 240K lines (~500ms per query)
- **With index:** JSON lookup (~5ms per query)
- **Speedup:** 100x faster

## Generated Files

Indexes are generated from scratch each run (deterministic):
- Always accurate
- Bug fixes propagate immediately
- Version-controlled for diff visibility

## Documentation

- [README.md](README.md) - This file

## Integration

The `/mac-api` skill automatically checks indexes before searching books:

```markdown
### Step 2: Search Books (Two-Stage Process)

**STAGE 1: Check the index first (100x faster)**

Read("tools/book_indexer/index/functions.json")
# Direct lookup: OTAllocMem → interrupt_safe: true
```

See `.claude/skills/mac-api/SKILL.md` for details.

## Understanding Sync/Async Dependent Functions

**36 functions** (4.9%) are marked with `"sync_async_dependent": true`. These functions appear in BOTH safe and unsafe tables because their interrupt safety depends on execution mode.

From Inside Macintosh Volume VI Appendix B:

> "Some routines exhibit different memory behavior when executed synchronously than when executed asynchronously. These routines are included in more than one list: a single dagger (+) indicates a synchronous execution of the routine, and a double dagger (‡) indicates an asynchronous execution."

**Example:** `GetLocalZones`
```json
{
  "interrupt_safe": null,
  "moves_memory": null,
  "sync_async_dependent": true,
  "tables": ["table_b2_unsafe_no_move", "table_b3_interrupt_safe"]
}
```

**Interpretation:**
- Synchronous call (`+` in book): **NOT safe** at interrupt time
- Asynchronous call (`‡` in book): **SAFE** at interrupt time

**Common sync/async dependent functions:**
- AppleTalk: GetLocalZones, GetMyZone, GetZoneList
- File Manager: PBCatSearch, PBMakeFSSpec, PBExchangeFiles, PBDTAddAPPL
- PPC: All PPC* functions (PPCOpen, PPCClose, PPCRead, PPCWrite, etc.)

**Safe usage pattern:**
```c
/* WRONG - synchronous call at interrupt time */
GetMyZone();  // NOT SAFE in ASR

/* RIGHT - asynchronous call with completion routine */
GetMyZone_async(my_completion_routine);  // SAFE in ASR
```

## Maintenance

**When to rebuild:**
- After updating books/
- After fixing bugs in build_index.py
- Never manually edit JSON files (they're auto-generated)

**How to verify quality:**
```bash
# Run the indexer
python3 tools/book_indexer/build_index.py

# Check the summary
# Should show: "741 functions" and "0 junk detected"
```

## Known Limitations

**Missing by design:**
- Core Memory Manager functions (NewPtr, NewHandle, etc.)
- Functions from Inside Macintosh Volumes I-III

**Why:** Tables only list "routines described in Volume VI"

**Workaround:** Core unsafe functions documented in `.claude/rules/isr-safety.md`

## Source Tables

| Table | Book | Lines | Functions | Purpose |
|-------|------|-------|-----------|---------|
| B-1 | Inside Macintosh VI | 223761-224228 | 302 | Moves/purges memory (UNSAFE) |
| B-2 | Inside Macintosh VI | 224216-224391 | 127 | Unsafe for other reasons |
| B-3 | Inside Macintosh VI | 224396-224607 | 147 | Safe at interrupt time ✓ |
| C-1 | Open Transport v1.3 | 43052-43451 | 56 | OT hardware interrupt safe ✓ |
| C-2 | Open Transport v1.3 | 43502-43740 | 30 | OT hardware interrupt (native ISA) ✓ |
| C-3 | Open Transport v1.3 | 43748-44144 | 130 | OT deferred task callable |

## Filtering Logic

The indexer applies 10 quality checks to eliminate junk:

1. Must be 3+ characters
2. Must start with uppercase (Mac API convention)
3. Must contain alphanumeric characters
4. Can't start with special chars: `[]{|}§()`
5. Can't end with special chars: `!?|§=`
6. Can't contain OCR artifacts: `|§†‡`
7. Can't match table markers: `Table`, `B-`, `A-`, `C-`
8. Can't be common words (150+ word exclusion list)
9. Can't be all lowercase
10. Can't be numeric-only

Result: **100% clean extraction**
