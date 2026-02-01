# Parsing Plan Files - Technical Guide

Detailed guide for parsing phase plan files to extract session information, status, and dependencies.

## Contents
- CRITICAL: File Size Warning
- Parsing the Session Scope Table
- Phase Dependencies
- Phase Status
- Determining Session Availability
- Complete Workflow Example (status, next, complete commands)
- Tips for Robust Parsing

## CRITICAL: File Size Warning

**NEVER use Read on plan files** - they are 500+ lines each (45K+ tokens total) and will exceed the 25K token limit.

**ALWAYS use Grep** to extract information from plan files:
- Use `output_mode: "content"` with `-n: true` to get line numbers
- Use `-C: 3` or `-C: 5` for context lines if needed
- Make multiple targeted Grep calls instead of trying to Read entire files

**Example - CORRECT approach:**
```
Grep(pattern: "^# PHASE", path: "plan/PHASE-1-FOUNDATION.md", output_mode: "content", -n: true)
Grep(pattern: "\\*\\*Status:\\*\\*", path: "plan/PHASE-1-FOUNDATION.md", output_mode: "content", -n: true)
Grep(pattern: "^\\| Session \\|", path: "plan/PHASE-1-FOUNDATION.md", output_mode: "content", -n: true, -C: 50)
```

**Example - WRONG approach (will fail):**
```
Read("plan/PHASE-1-FOUNDATION.md")  ❌ ERROR: File too large!
```

## Parsing the Session Scope Table

The Session Scope Table appears in each phase plan file and tracks session status.

### Table Format

```
| Session | Focus | Status | Files Created/Modified | Tests | Verify |
|---------|-------|--------|------------------------|-------|--------|
| 1.0 | Build System | [OPEN] | `Makefile` | None | make succeeds |
| **5.9** | **AppleTalk Integration** | [OPEN] | ... | ... | ... |
```

### Table Location

Use Grep to find the table:
```
Grep(pattern: "^\\| Session \\|", path: "plan/PHASE-{N}-*.md", output_mode: "content", -n: true, -C: 50)
```

This will return:
- The header row: `| Session | Focus | Status | ... |`
- The separator row: `|---------|-------|--------|...`
- All session rows (with ~50 lines of context)

### Parsing Table Rows

For each row after the separator:

1. **Split by `|` delimiter**
   - Column 0: Empty (before first `|`)
   - Column 1: Session number (e.g., "1.0", "**5.9**")
   - Column 2: Title/Focus (e.g., "Build System")
   - Column 3: Status (e.g., "[OPEN]", "[IN PROGRESS]", "[DONE]")
   - Columns 4+: Additional info (files, tests, verify criteria)

2. **Strip bold markers**
   - Session numbers may be bold: `**5.9**` → `5.9`
   - Titles may be bold: `**AppleTalk Integration**` → `AppleTalk Integration`
   - Use regex or string replace to remove `**`

3. **Extract status**
   - Look for `[OPEN]`, `[IN PROGRESS]`, or `[DONE]`
   - Status is in square brackets

### Example Parsing Logic

```python
import re

def parse_session_row(row):
    # Split by | and strip whitespace
    columns = [col.strip() for col in row.split('|')]

    # Skip if not enough columns or is separator row
    if len(columns) < 4 or '---' in columns[1]:
        return None

    # Extract session number (strip bold markers)
    session = re.sub(r'\*\*', '', columns[1])

    # Extract title (strip bold markers)
    title = re.sub(r'\*\*', '', columns[2])

    # Extract status
    status_match = re.search(r'\[(OPEN|IN PROGRESS|DONE)\]', columns[3])
    status = status_match.group(1) if status_match else None

    return {
        'session': session,
        'title': title,
        'status': status
    }
```

## Phase Dependencies

Dependencies are declared in the phase file header.

### Finding Dependencies

Use Grep to find the dependency line:
```
Grep(pattern: "\\*\\*Depends on:\\*\\*", path: "plan/PHASE-{N}-*.md", output_mode: "content", -n: true)
```

### Dependency Formats

- **No dependencies:**
  - `**Depends on:** None`
  - `**Depends on:** -`

- **Single dependency:**
  - `**Depends on:** Phase 0`
  - `**Depends on:** Phase 2`

- **Multiple dependencies:**
  - `**Depends on:** Phase 1, Phase 2`
  - `**Depends on:** Phase 0, Phase 3`

### Parsing Dependencies

```python
def parse_dependencies(dep_line):
    # Extract text after "Depends on:"
    match = re.search(r'\*\*Depends on:\*\* (.+)', dep_line)
    if not match:
        return []

    dep_text = match.group(1).strip()

    # Check for no dependencies
    if dep_text in ['None', '-']:
        return []

    # Extract phase numbers
    phases = re.findall(r'Phase (\d+)', dep_text)
    return [int(p) for p in phases]
```

## Phase Status

Phase status is declared in the header.

### Finding Phase Status

Use Grep to find the status line:
```
Grep(pattern: "\\*\\*Status:\\*\\*", path: "plan/PHASE-{N}-*.md", output_mode: "content", -n: true)
```

### Status Values

- `**Status:** [OPEN]` - Not started
- `**Status:** [IN PROGRESS]` - Some sessions complete, others in progress
- `**Status:** [DONE]` - All sessions complete

## Determining Session Availability

A session is available when:

1. **All dependent phases have status `[DONE]`**
   - Check phase dependencies
   - For each dependency, verify its status is `[DONE]`

2. **All earlier sessions in the same phase are `[DONE]`**
   - Sessions must be completed in order within a phase
   - Session 1.3 requires 1.1 and 1.2 to be `[DONE]`

### Example Availability Check

```python
def is_session_available(phase_num, session_num, all_phases):
    phase = all_phases[phase_num]

    # Check phase dependencies
    for dep_phase_num in phase['dependencies']:
        dep_phase = all_phases[dep_phase_num]
        if dep_phase['status'] != 'DONE':
            return False  # Blocked by dependency

    # Check earlier sessions in same phase
    for session in phase['sessions']:
        session_id = session['session']

        # If this is an earlier session and not done, we're blocked
        if session_id < session_num and session['status'] != 'DONE':
            return False

        # If this is the target session, check its status
        if session_id == session_num:
            return session['status'] != 'DONE'

    return False
```

## Complete Workflow Example

### For `status` command:

```bash
# 1. Find all phase files
Glob(pattern: "PHASE-*.md", path: "plan/")

# 2. For each phase file:
#    a. Extract phase number from filename
#    b. Extract phase title
Grep(pattern: "^# PHASE", path: "plan/PHASE-{N}-*.md", output_mode: "content", -n: true)

#    c. Extract status
Grep(pattern: "\\*\\*Status:\\*\\*", path: "plan/PHASE-{N}-*.md", output_mode: "content", -n: true)

#    d. Extract dependencies
Grep(pattern: "\\*\\*Depends on:\\*\\*", path: "plan/PHASE-{N}-*.md", output_mode: "content", -n: true)

#    e. Extract session table
Grep(pattern: "^\\| Session \\|", path: "plan/PHASE-{N}-*.md", output_mode: "content", -n: true, -C: 50)

#    f. Parse sessions and count by status

# 3. Display summary table
```

### For `next` command:

```bash
# 1. Get all phases and sessions (same as status)

# 2. For each phase in order (0, 1, 2, ...):
#    a. Check if dependencies satisfied
#    b. Find first non-DONE session
#    c. If found, that's the next available session
#    d. If not found, continue to next phase

# 3. Display recommended session
```

### For `complete <session>` command:

```bash
# 1. Parse session number (e.g., "1.4")
session_num = "1.4"
phase_num = 1

# 2. Find phase file
Glob(pattern: "PHASE-1-*.md", path: "plan/")

# 3. Find session row in table
Grep(pattern: "^\\| 1\\.4 \\|", path: "plan/PHASE-1-*.md", output_mode: "content", -n: true)

# 4. Edit to change status
Edit(
  file_path: "plan/PHASE-1-*.md",
  old_string: "| 1.4 | ... | [OPEN] |",
  new_string: "| 1.4 | ... | [DONE] |"
)

# 5. Check if all sessions done
Grep(pattern: "\\[OPEN\\]", path: "plan/PHASE-1-*.md", output_mode: "content")

# 6. If no [OPEN] sessions remain, update phase status
Edit(
  file_path: "plan/PHASE-1-*.md",
  old_string: "**Status:** [IN PROGRESS]",
  new_string: "**Status:** [DONE]"
)

# 7. Run next command to show what's available
```

## Tips for Robust Parsing

1. **Use regex sparingly** - Plan files have consistent formatting
2. **Handle bold markers** - Session numbers and titles may be bold
3. **Trim whitespace** - Table cells may have extra spaces
4. **Verify line counts** - Check you got expected number of sessions
5. **Error handling** - Check if Grep returned any results before parsing
6. **Use -C flag** - Get context lines when extracting tables
7. **Multiple Grep calls** - Better than one huge Read that will fail
