---
name: backport
description: Suggest tool improvements to cherry-pick to starter-template branch. Use after completing several sessions to identify which commits contain tooling updates (skills, hooks, scripts) that should be backported vs SDK implementation code that should stay on main. Analyzes recent commits and provides ready-to-run cherry-pick commands.
argument-hint: [commit-count]
---

# Backport Tool Improvements

Analyzes recent commits on main and suggests which tool improvements should be cherry-picked back to the `starter-template` branch.

## Usage

```bash
/backport          # Check last 10 commits
/backport 20       # Check last 20 commits
```

## What This Skill Does

The starter-template branch should remain a clean starting point with:
- Latest tooling improvements (.claude/, tools/)
- Updated documentation about workflows
- No SDK implementation code (src/, include/, tests/)

This skill helps you identify which commits to backport.

## Implementation Approach

Use the **Bash tool** to run git commands and parse output. Process the commit log line-by-line to categorize commits. Present formatted analysis with actionable commands.

## Process

**Step 1:** Verify you're on main branch:
```bash
git rev-parse --abbrev-ref HEAD
```
If not on main, warn and exit.

**Step 2:** Get commits to analyze:
```bash
# Use argument provided by user, default to 10 if not specified
# Arguments are passed as the skill argument (e.g., "/backport 20")
COUNT=10  # or use the number from arguments

# Get list of commits
git log --oneline -n $COUNT
```

**Step 3:** For each commit, categorize based on changed files:

Get files changed in each commit:
```bash
git show --name-only --pretty="" <commit-hash>
```

Apply categorization logic:
- **TOOL**: All changed files are in tooling paths (see File Path Matching Logic)
- **SDK**: All changed files are in SDK paths
- **MIXED**: Some tooling files AND some SDK files
- Check if already backported (see Important Checks section)

**Step 4:** Format output for each category:

For **TOOL** commits:
```
✓ <hash> - <message>
  Files: <list tooling files>
  Command: git cherry-pick <hash>
```

For **SDK** commits:
```
✗ <hash> - <message>
  Files: <list SDK files>
  Reason: SDK implementation, not tooling
```

For **MIXED** commits:
```
⚠ <hash> - <message>
  Tooling: <list tooling files>
  SDK: <list SDK files>
  Suggest manual backport:
    git checkout starter-template
    git checkout main -- <tooling-file1> <tooling-file2>
    git commit -m "Backport: <description>"
```

**Step 5:** Show summary:
```
Summary:
- X commits ready to cherry-pick
- Y commits to skip (SDK code)
- Z commits need manual review
```

**Step 6:** If any TOOL commits found (not MIXED, not already backported), show batch command:
```bash
# Only include commits categorized as pure TOOL improvements
git checkout starter-template
git cherry-pick <hash1> <hash2> <hash3>  # Only TOOL commit hashes
git push origin starter-template
git checkout main
```

## Backport Criteria

**DO backport:**
- Skill improvements (`.claude/skills/`)
- Hook fixes (`.claude/hooks/`)
- Agent enhancements (`.claude/agents/`)
- Tool updates (`tools/`)
- Workflow documentation (CLAUDE.md, .claude/rules/)
- Build script improvements (`scripts/`)

**DON'T backport:**
- SDK implementation (`src/`, `include/`)
- Test code (`tests/`)
- Phase completion markers in plan files
- Build artifacts or generated files

**MAYBE backport (review case-by-case):**
- CLAUDE.md updates (if adding general guidance vs SDK-specific)
- README updates (if improving template explanation vs SDK status)
- Plan file updates (if clarifying requirements vs marking sessions done)

## Example Output

```
Backport Analysis (last 10 commits)
===================================

TOOL IMPROVEMENT (backport recommended):
✓ a327809 - Fix ISR safety hook to catch TickCount violations
  Files: .claude/hooks/isr-safety-check.sh
  Command: git cherry-pick a327809

TOOL IMPROVEMENT (backport recommended):
✓ b123456 - Add prerequisite checks to /build skill
  Files: .claude/skills/build/SKILL.md
  Command: git cherry-pick b123456

SDK CODE (skip):
✗ c789012 - Implement PT_Log POSIX backend
  Files: src/log/pt_log_posix.c, tests/test_log_posix.c
  Reason: SDK implementation, not tooling

MIXED (manual review):
⚠ d456789 - Update CLAUDE.md with buffer sizing, fix /review skill
  Tooling files: .claude/skills/review/SKILL.md
  SDK files: CLAUDE.md (mixed SDK/general content)
  Suggest: git checkout starter-template
           git checkout main -- .claude/skills/review/SKILL.md
           # Review CLAUDE.md changes manually

Summary:
- 2 commits ready to cherry-pick
- 1 commit to skip (SDK code)
- 1 commit needs manual review
```

## Integration with Workflow

Run `/backport` after completing a few SDK sessions to identify tool improvements worth sharing back to the template:

```bash
# After implementing Phase 0 Session 1-3
/backport 15

# Review suggestions, then:
git checkout starter-template
git cherry-pick <hash1> <hash2>
git push origin starter-template
git checkout main
```

## Important Checks

**Check if commit already backported:**

Since cherry-picked commits get new hashes on starter-template, check by commit message:
```bash
# Get recent commit messages on starter-template
git log starter-template --oneline -n 50 --pretty=format:"%s"
```

If a commit message from main matches one on starter-template (accounting for "Backport:" prefix), mark as [ALREADY BACKPORTED].

**Note:** Don't perform destructive checks (like test cherry-picks) - keep analysis read-only.

## File Path Matching Logic

Categorize files by checking if path starts with or matches:

| Path Pattern | Category | Action | Reason |
|--------------|----------|--------|--------|
| `.claude/**` | Tooling | Backport | Skills, hooks, agents, rules |
| `tools/**` | Tooling | Backport | Build scripts, validators |
| `scripts/**` | Tooling | Backport | Automation scripts |
| `.github/workflows/**` | Tooling | Backport | CI/CD workflows |
| `docker/**` | Tooling | Backport | Build environment |
| `docs/STARTER-TEMPLATE.md` | Tooling | Backport | Template documentation |
| `docs/CLAUDE-CODE-SETUP.md` | Tooling | Backport | Setup guide |
| `.clang-format` | Tooling | Backport | Code formatting config |
| `src/**` | SDK | Skip | Implementation code |
| `include/*.{c,h}` | SDK | Skip | Headers (implementation) |
| `tests/**` | SDK | Skip | Test code |
| `Makefile` | SDK | Skip | Build configuration |
| `build/**` | SDK | Skip | Build artifacts |
| `CLAUDE.md` | Review | Manual | May be general or SDK-specific |
| `README.md` | Review | Manual | May be template or status |
| `plan/**` | Review | Manual | May be clarifications or completion markers |
| `books/**` | Review | Manual | Only if adding new reference material |

## Notes

- This skill is read-only (analyzes commits, doesn't modify branches)
- Always review suggestions before cherry-picking
- Test on starter-template branch after backporting
- Consider running `/backport` weekly or after major tool improvements
- Commits are analyzed in reverse chronological order (newest first)
- This skill only exists on main (backport it to starter-template if you want it there too)

## Future Enhancements

Potential additions:
- Detect conflicts before suggesting cherry-pick
- Auto-generate backport PR descriptions
- Track which commits have already been backported (via git notes or tags)
- Suggest batch cherry-pick commands for multiple commits
