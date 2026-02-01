---
name: check-isr
description: Validate callback functions for interrupt-time safety. Use when implementing or editing MacTCP ASR, Open Transport notifier, or AppleTalk callback code. Also use before committing Mac platform code to verify no ISR violations exist.
argument-hint: [file-or-directory]
---

# ISR Safety Validator

Scans MacTCP ASR, Open Transport notifier, and AppleTalk callback functions for interrupt-time safety violations.

## Usage

```
/check-isr src/mactcp/
/check-isr src/mactcp/tcp_mactcp.c
/check-isr
```

Without arguments, scans all Mac-specific source directories.

## Process

1. **Verify prerequisites**:
   ```bash
   # Check validator exists
   if [[ ! -f tools/validators/isr_safety.py ]]; then
       echo "❌ ISR validator not found at tools/validators/isr_safety.py"
       echo "   This tool should be created in Phase 1 or 2"
       exit 1
   fi

   # Check Python available
   if ! which python3 >/dev/null 2>&1; then
       echo "❌ Python 3 not found"
       echo "   Install: sudo apt install python3"
       exit 1
   fi

   # Setup virtual environment if needed
   if [[ ! -d tools/.venv ]]; then
       echo "Creating Python virtual environment..."
       python3 -m venv tools/.venv
       source tools/.venv/bin/activate
       if [[ -f tools/requirements.txt ]]; then
           pip install -r tools/requirements.txt
       fi
   else
       source tools/.venv/bin/activate
   fi

   echo "✓ Prerequisites OK"
   ```

2. **Run the validator**:
   ```bash
   python tools/validators/isr_safety.py ${ARGUMENTS:-src/mactcp src/opentransport src/appletalk}
   ```

2. **For each violation found**:
   - Show the file, line number, and callback function name
   - Show the forbidden function call
   - Explain why it's forbidden (with documentation reference)
   - Suggest the correct pattern

3. **If no violations**: Confirm the code is ISR-safe

## What It Checks

The validator detects callback functions by matching:
- Function names ending in `_asr`, `_notifier`, `_completion`, `_callback`
- Functions with `pascal` keyword and MacTCP/OT callback signatures
- Functions matching known ASR/notifier parameter patterns

Within those functions, it checks for calls to:

| Category | Examples | Why Forbidden |
|----------|----------|---------------|
| Memory allocation | malloc, NewPtr, NewHandle | Heap operations at interrupt time |
| Memory operations | memcpy, BlockMove | May use unsafe implementations |
| Timing | TickCount | Not in Table B-3 interrupt-safe list |
| I/O | printf, FSRead, FSWrite | File/device operations forbidden |
| Sync network | TCPSend, OTConnect | Synchronous calls in async context |
| Toolbox | GetResource, DrawString | Most Toolbox not interrupt-safe |

## Common Fixes

| Violation | Correct Pattern |
|-----------|-----------------|
| memcpy in callback | Use `pt_memcpy_isr()` |
| TickCount in callback | Set `timestamp = 0`, let main loop timestamp |
| malloc in callback | Use pre-allocated buffers from context struct |
| Sync network call | Use async version with completion callback |
| printf/logging | Set a flag, print from main loop |

## Example Output

```
src/mactcp/tcp_mactcp.c
Line  | Callback        | Forbidden Call | Reason
------|-----------------|----------------|----------------------------------
142   | tcp_asr         | memcpy         | May use BlockMove internally
156   | tcp_asr         | TickCount      | Not in Table B-3 interrupt-safe
189   | udp_asr         | malloc         | Dynamic allocation forbidden
```

## Integration

The ISR Safety Validator is also integrated as a pre-edit hook. When editing files in `src/mactcp/`, `src/opentransport/`, or `src/appletalk/`, the hook will block edits that introduce violations.

---

## Example Workflows

### After Implementing Mac Code
```bash
# Implemented MacTCP TCP driver
/check-isr src/mactcp/tcp_mactcp.c
# → Shows violations if any

# Fix violations, then verify all Mac code
/check-isr
# → Checks all src/mactcp/, src/opentransport/, src/appletalk/
```

### During Development
```bash
# Write code with callbacks...
# Edit tool auto-runs isr-safety-check.sh hook
# → Hook BLOCKS edit if violation detected

# Fix the violation
# Re-attempt edit
# → Hook allows edit through ✓
```

### Pre-Commit Check
```bash
/check-isr
# → Verify all Mac code is ISR-safe before committing
```

### Debugging a Violation

When a hook blocks an edit with an ISR violation:

1. **Understand why:** Check `.claude/rules/isr-safety.md` for the forbidden function
2. **Find all instances:** `/check-isr src/mactcp/` shows violations with line numbers
3. **Fix using safe pattern:** Replace with ISR-safe alternative (see Common Fixes table)
4. **Verify fix:** `/check-isr` should report no violations

Example: TickCount() → Set flag in callback, timestamp in main loop

---

## Reference

See CLAUDE.md sections:
- "MacTCP ASR Rules"
- "Open Transport Notifier Rules"
- "AppleTalk ADSP Callback Rules"

Source: Inside Macintosh Volume VI, Table B-3 - "Routines That May Be Called at Interrupt Time"
