# Session Verification Checklist

After all tasks in a session are complete, run these verification steps before marking the session as [DONE].

## 1. Build Verification

### POSIX Platform
```bash
# Clean build
make clean && make

# If tests exist for this session
make test
```

### Mac Platforms
Use the build skill to verify cross-platform compilation:
```bash
/build all
```

## 2. Session-Specific Tests

Run any tests defined in the session's acceptance criteria. These may include:
- Unit tests for new functions
- Integration tests for platform-specific code
- Regression tests for modified functionality

## 3. Check Acceptance Criteria

Review the session specification and verify all acceptance criteria are met:
- [ ] All required files created/modified
- [ ] All struct definitions match specification
- [ ] All functions implemented with correct signatures
- [ ] All CRITICAL/IMPORTANT notes addressed
- [ ] Code follows quality gates (function length, file size, complexity)

## 4. Memory/Leak Check

For POSIX-testable code, run valgrind to detect memory issues:
```bash
valgrind --leak-check=full ./tests/test_{relevant}
```

Look for:
- Memory leaks (blocks definitely lost)
- Invalid reads/writes
- Use of uninitialized values

## 5. Quality Gates (from CLAUDE.md)

Verify code meets these thresholds:
- **Max function length:** 100 lines (prefer 50)
- **Max file size:** 500 lines
- **Cyclomatic complexity:** 15 max per function
- **Compiler warnings:** Treat as errors
- **Coverage target:** 10% minimum (POSIX)

## 6. ISR-Safety Check (Mac Platforms Only)

For MacTCP, Open Transport, or AppleTalk code:
```bash
/check-isr src/mactcp/     # or src/opentransport/ or src/appletalk/
```

Ensure no violations are reported.

## 7. Code Style

Verify consistent code style:
- Proper indentation and formatting
- Comments where logic isn't self-evident
- Function and variable naming follows conventions
- No dead code or commented-out blocks

## 8. Spec Compliance (Spec-Kit Mode Only)

For each implemented task with a `[USn]` label:
- Read the corresponding user story's acceptance criteria from spec.md
- Verify each acceptance criterion is met by the implementation
- Check that the implementation doesn't exceed scope (no unrequested features)

## 9. Constitution Compliance (Spec-Kit Mode Only)

If a constitution was loaded:
- Verify implementation decisions align with constitution principles
- Check that no non-negotiables were violated
- Verify out-of-scope items weren't accidentally implemented

## 10. Cross-Task Consistency (After Parallel Batches)

After parallel batch execution:
- Verify tasks that were implemented in parallel are compatible
- Check for conflicting assumptions (different error codes, naming conventions)
- Verify shared headers modified by multiple tasks don't have conflicts
- Ensure interfaces defined by one task match usage by another

## Verification Failure Handling

If any verification step fails:
1. **Do NOT mark task/session as complete** (`[X]` or `[DONE]`)
2. **Document the specific failure**
3. **Fix the issue**
4. **Re-run verification from Step 1**

Only proceed to status update when ALL verification passes.
