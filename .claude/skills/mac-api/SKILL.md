---
name: mac-api
description: Search authoritative Classic Mac reference books (Inside Macintosh, MacTCP Guide, Open Transport docs) for API documentation, interrupt safety rules, error codes, and implementation details. Returns line-level citations from official Apple documentation.
argument-hint: [question or function name]
---

# Mac API Research Skill

Search authoritative Classic Macintosh reference books for API documentation with line-level citations.

## Usage

```
/mac-api TickCount
/mac-api is OTAllocMem safe at interrupt time?
/mac-api what does error connectionClosing mean?
/mac-api show me Table B-3
/mac-api how does TCPPassiveOpen work?
/mac-api dspCLListen
```

## What You Get

- **Direct answers** from official Apple documentation
- **Exact quotes** with book name and line numbers
- **Cross-references** to related APIs and tables
- **Suggestions** for adding verified content to project rules

## How It Works

### Step 1: Check Project Rules First

Read the relevant rules file to see if the information is already verified:
- MacTCP question → `.claude/rules/mactcp.md`
- Open Transport question → `.claude/rules/opentransport.md`
- AppleTalk/ADSP/NBP question → `.claude/rules/appletalk.md`
- ISR/interrupt safety → `.claude/rules/isr-safety.md`

**If rules have `> **Verified:**` markers with line references:**
- Information is already fact-checked
- Trust it and cite the verification marker
- **Skip book search** - just report from rules

### Step 2: Search Books (Two-Stage Process)

**STAGE 1: Check the index first (100x faster)**

Pre-built indices in `tools/book_indexer/index/`:
- `functions.json` - 741 functions with interrupt-safety info
- `tables.json` - 6 critical tables (B-1, B-2, B-3, C-1, C-2, C-3)
- `error_codes.json` - 185 error codes
- `keywords.json` - Common concepts with line ranges

```bash
# Check if function exists in index
Read("tools/book_indexer/index/functions.json")
# Look for the function, get interrupt_safe flag and table references
```

**Understanding interrupt_safe flags:**
- `true` - Safe at interrupt time
- `false` - Unsafe (moves memory or not reentrant)
- `null` - Unknown OR sync/async dependent

**STAGE 2: Targeted Grep + Read**

If not in index:
```bash
# 1. Grep with context to find line numbers
Grep(pattern: "TickCount", path: "books/Inside_Macintosh_Volume_VI_1991.txt",
     output_mode: "content", -n: true, -C: 10)

# 2. Read specific line range
Read(file_path: "books/Inside_Macintosh_Volume_VI_1991.txt",
     offset: 224396, limit: 220)
```

**For known tables, jump directly:**

See `references/key-line-references.md` for complete lookup tables.

Quick reference:
| Table | Book | Lines | Use For |
|-------|------|-------|---------|
| Table B-3 | Inside Macintosh VI | 224396-224607 | Interrupt-safe routines |
| Table C-1 | Networking OT | 43052-43451 | OT hardware interrupt-safe |
| MacTCP ASR rules | MacTCP Guide | 2150-2156, 4226-4232 | ASR restrictions |
| ADSP callbacks | Programming AppleTalk | 5924-5926 | Callback rules |

### Step 3: Report Findings

**When Rules Have Verified Content:**

```
## Answer

[Direct answer]

## Source

**`.claude/rules/[file].md`** - [section name]

Already verified from [Book Name] (Lines X-Y)
```

**When Searching Books:**

```
## Answer

[Direct answer]

## Source

**[Book Name]** (Lines X-Y):
> [Exact quote from the book]

## Suggested Addition

**File:** `.claude/rules/[file].md`
**Section:** [where to add]
**Content:**
> **Verified:** [Book Name] (Lines X-Y): "[key quote]"
> [Information to add]
```

**When Rules Contradict Books:**

```
## Answer

[Corrected answer based on books]

## CORRECTION NEEDED

**Rules say:** [what rules claim]
**Books say:** [what books say] - [Book Name] (Lines X-Y)

The books are authoritative. Rules should be updated.
```

## Reference Books

**For complete book descriptions, see `references/book-catalog.md`**

Quick reference:
| Book | Primary Use |
|------|-------------|
| MacTCP_Programmers_Guide_1989.txt | MacTCP API, ASR rules |
| NetworkingOpenTransport.txt | Open Transport API, Table C-1 |
| Programming_With_AppleTalk_1991.txt | AppleTalk/ADSP/NBP, callbacks |
| Inside_Macintosh_Volume_VI_1991.txt | Table B-3 (interrupt-safe routines) |
| Inside_Macintosh_Volume_V_1986.txt | Completion routines, Deferred Tasks |

## Important Rules

1. **NEVER Read entire books** (240K+ lines) - Use Grep first, then targeted Read
2. **Verified rules are trustworthy** - If marked with `> **Verified:**`, use directly
3. **Include line numbers** - Makes verification easy
4. **Quote the books** - Exact text is more valuable than paraphrase
5. **Books are authoritative** - If rules don't match books, books win
6. **Suggest improvements** - Help keep the rules accurate and complete

## Example Queries

### "Is TickCount safe at interrupt time?"

1. Check `.claude/rules/isr-safety.md` - look for TickCount
2. Check `tools/book_indexer/index/functions.json` for TickCount entry
3. Read Table B-3 directly: `Read("books/Inside_Macintosh_Volume_VI_1991.txt", offset: 224396, limit: 220)`
4. Report: "TickCount is NOT in Table B-3, therefore NOT interrupt-safe"

### "What does error connectionClosing mean?"

1. Check `tools/book_indexer/index/error_codes.json`
2. If found, jump to line reference
3. If not, Grep: `Grep(pattern: "connectionClosing", path: "books/MacTCP_Programmers_Guide_1989.txt", -C: 5)`
4. Report with exact quote and line number

### "How does TCPPassiveOpen work?"

1. Check `.claude/rules/mactcp.md` for verified content
2. Grep MacTCP guide: `Grep(pattern: "TCPPassiveOpen", path: "books/MacTCP_Programmers_Guide_1989.txt", -C: 15)`
3. Read broader context around matches
4. Report parameter block structure, lifecycle, one-shot behavior

## Trust Hierarchy

1. **Rules with `> **Verified:**` markers** → TRUST directly (already fact-checked)
2. **Rules without verification** → Search books to verify before answering
3. **Information not in rules** → Search books, suggest adding to rules
4. **User asks for more detail** → Always search books for additional context

## Integration

This skill uses the same reference materials and search strategies as the project's implementation rules. When you find new verified information, suggest adding it to the appropriate `.claude/rules/*.md` file with proper verification markers.

---

**See `references/` folder for:**
- `book-catalog.md` - Complete book descriptions
- `key-line-references.md` - Quick lookup tables
- `search-strategy.md` - Detailed search patterns and anti-patterns
