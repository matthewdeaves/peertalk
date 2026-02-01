# Search Strategy and Examples

Efficient patterns for searching the 240K+ lines of documentation.

## Two-Stage Search Process

**STEP 1: Check the index first (100x faster)**

The book indexer has pre-processed critical information:
- `tools/book_indexer/index/functions.json` - 741 functions with interrupt-safety info
- `tools/book_indexer/index/tables.json` - 6 critical tables (B-1, B-2, B-3, C-1, C-2, C-3)
- `tools/book_indexer/index/error_codes.json` - 185 error codes
- `tools/book_indexer/index/keywords.json` - Common concepts with line ranges

**For function safety queries:**
```
Read("tools/book_indexer/index/functions.json")
# Check if function exists, get interrupt_safe flag and table references
# Example: "OTAllocMem" → {"interrupt_safe": true, "tables": ["table_c1_ot_hw_interrupt"]}
```

**Understanding interrupt_safe flags:**
- `true` - Safe to call at interrupt/hardware interrupt time
- `false` - Unsafe (moves memory or not reentrant)
- `null` - Unknown OR depends on sync vs async execution

**Check for sync_async_dependent:**
```json
"GetMyZone": {
  "interrupt_safe": null,
  "sync_async_dependent": true,
  "tables": ["table_b2_unsafe_no_move", "table_b3_interrupt_safe"]
}
```
This means: Synchronous call = UNSAFE, Asynchronous call = SAFE
(36 functions have this pattern - see Inside Macintosh Volume VI Appendix B legend)

**For table lookups:**
```
Read("tools/book_indexer/index/tables.json")
# Get exact line ranges for tables
# Example: table_b3_interrupt_safe → lines [224396, 224607]
# Then: Read(offset: 224396, limit: 220) to get the full table
```

**For error codes:**
```
Read("tools/book_indexer/index/error_codes.json")
# Direct lookup of error code meanings and locations
```

**STEP 2: If not in index, use Grep strategically**

1. **NEVER Read entire books (240K+ lines)**
   - Use Grep with `output_mode: "content"` and `-n: true` to get line numbers
   - Use `-C: 5` or `-C: 10` for context lines around matches
   - Start with specific terms, broaden if no results

2. **Multi-stage narrowing for large files:**
   - First grep: Find the chapter/section (e.g., "Notifier", "ASR")
   - Review line numbers from first grep
   - Second grep: Search within that line range using `offset` and `head_limit`
   - Or use Read with specific offset/limit based on line numbers found

3. **Use the Key Line References (see key-line-references.md):**
   - If you know you need Table B-3, go directly to lines 224396-224607
   - If you need OT Table C-1, go to lines 43052-43451
   - Use Read with offset/limit to read just those ranges

4. **Look for authoritative statements:**
   - "must", "shall", "cannot", "always", "never"
   - "do not", "forbidden", "required", "safe to call"
   - Tables, error code lists, parameter definitions

5. **Read with precision:**
   - If grep found line 5000, use `Read offset: 4990, limit: 50`
   - Don't read more than 100-200 lines at a time unless necessary
   - Use multiple targeted reads rather than one giant read

## Example Search Queries

**Efficient pattern: Grep with context, then targeted Read**

### Example 1: Find if TickCount() is interrupt-safe

```
# Step 1: Check known location first (Table B-3 is at lines 224396-224607)
Read(file_path: "books/Inside_Macintosh_Volume_VI_1991.txt",
     offset: 224396, limit: 220)
# Search the read content for "TickCount"

# Step 2: If not found, grep the whole file with context
Grep(pattern: "TickCount",
     path: "books/Inside_Macintosh_Volume_VI_1991.txt",
     output_mode: "content", -n: true, -C: 10)
```

### Example 2: Find OTAllocMem restrictions in notifier

```
# Step 1: Grep for "OTAllocMem" with context
Grep(pattern: "OTAllocMem",
     path: "books/NetworkingOpenTransport.txt",
     output_mode: "content", -n: true, -C: 10)
# Returns matches with line numbers, e.g., line 9143

# Step 2: Read broader context around the match
Read(file_path: "books/NetworkingOpenTransport.txt",
     offset: 9140, limit: 50)
```

### Example 3: Find MacTCP error code meaning

```
# Step 1: Grep for the specific error
Grep(pattern: "connectionClosing",
     path: "books/MacTCP_Programmers_Guide_1989.txt",
     output_mode: "content", -n: true, -C: 5)

# Or search the known error codes section (lines 5939-6120)
Read(file_path: "books/MacTCP_Programmers_Guide_1989.txt",
     offset: 5939, limit: 181)
```

### Example 4: Find ADSP userFlags behavior

```
# Grep for "userFlags" with context
Grep(pattern: "userFlags",
     path: "books/Programming_With_AppleTalk_1991.txt",
     output_mode: "content", -n: true, -C: 15)
```

### Example 5: Two-stage narrowing for large searches

```
# Stage 1: Find the chapter/section
Grep(pattern: "Chapter.*Notifier",
     path: "books/NetworkingOpenTransport.txt",
     output_mode: "content", -n: true)
# Returns: Chapter starts at line 5700

# Stage 2: Grep within that chapter
Grep(pattern: "kOTProviderWillClose",
     path: "books/NetworkingOpenTransport.txt",
     output_mode: "content", -n: true, -C: 10,
     offset: 5700, head_limit: 5000)
```

## Anti-Patterns (Don't Do This)

❌ **BAD:** Read entire book (240K lines!)
```
Read("books/Inside_Macintosh_Volume_VI_1991.txt")
```

✅ **GOOD:** Grep first, then targeted read
```
Grep(pattern: "TickCount", path: "books/Inside_Macintosh_Volume_VI_1991.txt",
     output_mode: "content", -n: true, -C: 5)
# See it's mentioned at line 15234
Read("books/Inside_Macintosh_Volume_VI_1991.txt", offset: 15220, limit: 30)
```

## Special Handling: Safety Questions

For "Can I call X from ASR/notifier/callback?" questions:

1. Check `.claude/rules/isr-safety.md` and platform-specific rules
2. **If the function is listed with a `> **Verified:`** marker:**
   - Trust the rule - it's already been fact-checked
   - Cite the verification marker in your answer
3. **If the function is NOT listed, or has no verification:**
   - Search the books to find a definitive answer
   - Key locations:
     - Inside Macintosh Volume VI Table B-3 (lines 224396-224606)
     - For OT: NetworkingOpenTransport Table C-1 (lines 43041-43495)
     - For MacTCP: MacTCP_Programmers_Guide ASR section (lines 2150-2156, 4226-4232)
     - For ADSP: Programming_With_AppleTalk completion routine rules (lines 5924-5926)
4. **Be conservative** - if not explicitly listed as safe in the books, it is UNSAFE
5. **Suggest adding to rules** if you find new verified information
