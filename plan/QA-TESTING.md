# QA Testing

> **Tracking for manual hardware testing and verification**

## Overview

This document tracks issues found during manual testing on real hardware, especially for the MacTCP and Open Transport implementations. Emulators are used for build validation, but final verification must happen on target hardware.

## Issue Log

| ID | Summary | Platform | Status | Priority | Assigned |
|----|---------|----------|--------|----------|----------|

*No issues logged yet. Add rows as issues are discovered during hardware testing.*

### Status Key
- **[OPEN]** - Logged, not yet started
- **[IN PROGRESS]** - Actively being researched or fixed
- **[READY TO TEST]** - Code changed, awaiting verification on target hardware
- **[DONE]** - Verified working on target hardware
- **[CLOSED]** - Won't fix, out of scope, or deferred

### State Transitions
- Issue discovered: → `[OPEN]`
- Research/fix started: `[OPEN]` → `[IN PROGRESS]`
- Code committed: `[IN PROGRESS]` → `[READY TO TEST]`
- Verification passed: `[READY TO TEST]` → `[DONE]`
- Test failed: `[READY TO TEST]` → `[IN PROGRESS]`
- Out of scope: Any → `[CLOSED]`

---

## Session Scope

This section groups related issues into focused fix sessions.

| Session | Issue(s) | Status | Focus |
|---------|----------|--------|-------|

*No fix sessions defined yet. Group related issues into sessions as they are discovered.*

---

## How to Run QA Sessions

### Starting a Research Session
```
Read QA-TESTING.md and research Session QA-A.
Investigate how the existing codebase handles [issue area].
Check CLAUDE.md and the relevant MacTCP/OT guides for patterns.
Update the Research Findings section for the issue with what you discover.
```

### Starting a Fix Session
```
Read QA-TESTING.md and implement the fix for Session QA-A.
Follow the tasks defined in the session plan. The research findings show
the patterns to follow.
```

### After Each Session
1. Confirm fix is deployed to target hardware (build succeeded, no errors)
2. Clear caches (if applicable)
3. Verify new code is running
4. Test fix on target device
5. Update issue status to `[READY TO TEST]` or `[DONE]`
6. Run `/clear`

---

## Fix Sessions

*Fix sessions will be added here as issues are discovered. Use the template below:*

### Session QA-X: {Title}
> **Status:** [OPEN]
> **Issues:** QA-N, QA-M
> **Target Hardware:** {Mac SE / Performa 6200 / PPC Mac / etc.}

#### Problem
{Description of the problem, screenshots, reproduction steps}

#### Research Findings
{Investigation of existing patterns, design intent, architectural constraints}

#### Tasks
- [ ] {Task 1 based on research}
- [ ] {Task 2 based on research}

#### Verification Criteria
- [ ] {How to confirm the fix works on the target hardware}
