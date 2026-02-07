---
name: backport
description: Sync tooling and plans to starter-template branch. Copies latest .claude/, docker/, tools/, scripts/, plan/, and CI workflows from the current branch to starter-template, resetting plan statuses to OPEN. Use periodically to keep the template current.
argument-hint: "[--dry-run]"
---

# Sync Tooling to Starter Template

Syncs the latest tooling files and phase plans from the development branch to `starter-template`, keeping it current with improvements to Claude Code setup, Docker environment, build scripts, and implementation plans.

## Usage

```bash
/backport              # Sync and commit
/backport --dry-run    # Preview what would change
```

## What Gets Synced

| Directory/File | Purpose |
|----------------|---------|
| `.claude/` | Skills, hooks, rules, agents, MCP servers |
| `docker/` | Dockerfile, docker-compose |
| `tools/` | Build scripts, validators, metrics |
| `scripts/` | Automation scripts |
| `.github/workflows/` | CI/CD pipelines |
| `dashboard/` | Metrics dashboard |
| `plan/` | Phase plans (statuses reset to OPEN) |
| `.clang-format` | Code formatting |
| `.gitignore` | Ignore patterns |
| `.mcp.json.example` | MCP configuration template |

## What Does NOT Get Synced

| Path | Reason |
|------|--------|
| `src/` | SDK implementation code |
| `include/` | SDK headers |
| `tests/` | Test code (SDK-specific) |
| `build/` | Build artifacts |
| `packages/` | Built packages |
| `CLAUDE.md` | Has SDK-specific sections (review manually) |
| `README.md` | Has SDK status info (review manually) |

## Process

**Step 1:** Verify on development branch
```bash
CURRENT=$(git rev-parse --abbrev-ref HEAD)
if [[ "$CURRENT" != "develop" && "$CURRENT" != "main" ]]; then
    echo "Run from develop or main branch"
    exit 1
fi
```

**Step 2:** Check for uncommitted changes
```bash
if ! git diff --quiet || ! git diff --cached --quiet; then
    echo "Commit or stash changes first"
    exit 1
fi
```

**Step 3:** Switch to starter-template
```bash
git checkout starter-template
git pull origin starter-template
```

**Step 4:** Sync files from development branch
```bash
git checkout $CURRENT -- \
    .claude/ \
    docker/ \
    tools/ \
    scripts/ \
    .github/workflows/ \
    dashboard/ \
    plan/ \
    .clang-format \
    .gitignore \
    .mcp.json.example
```

**Step 5:** Reset plan statuses to OPEN
```bash
# Reset phase-level statuses (DONE, [DONE], [DONE] ✓) to OPEN
for f in plan/PHASE-*.md; do
    sed -i 's/> \*\*Status:\*\* \[DONE\] ✓/> **Status:** OPEN/' "$f"
    sed -i 's/> \*\*Status:\*\* \[DONE\]/> **Status:** OPEN/' "$f"
    sed -i 's/> \*\*Status:\*\* DONE/> **Status:** OPEN/' "$f"
done

# Remove "ALL SESSIONS COMPLETE" lines
sed -i '/\*\*Status:\*\* ✅ ALL SESSIONS COMPLETE/d' plan/PHASE-*.md

git add plan/
```

**Step 6:** Show what changed
```bash
git status
git diff --cached --stat
```

**Step 7:** If `--dry-run`, revert and exit
```bash
git checkout .
git checkout $CURRENT
echo "Dry run complete - no changes made"
exit 0
```

**Step 8:** Commit and push
```bash
git commit -m "Sync tooling from $CURRENT ($(date +%Y-%m-%d))"
git push origin starter-template
```

**Step 9:** Return to development branch
```bash
git checkout $CURRENT
echo "✓ Synced to starter-template"
```

## Example Session

```
User: /backport --dry-run

Claude: Previewing sync to starter-template...

Switching to starter-template... ✓
Syncing from develop... ✓

Files that would change:
  .claude/skills/deploy/SKILL.md     | 45 +++++----
  .claude/skills/backport/SKILL.md   | 180 ++++++++++++----------
  docker/Dockerfile                   | 12 ++
  .github/workflows/ci.yml           | 8 +-
  tools/metrics/upload_to_pages.sh   | 3 +-

5 files changed

Dry run complete - no changes made.
Run `/backport` to apply these changes.
```

```
User: /backport

Claude: Syncing tooling to starter-template...

Current branch: develop
Switching to starter-template... ✓
Pulling latest... ✓
Syncing files from develop... ✓

Changes:
  .claude/skills/deploy/SKILL.md     | 45 +++++----
  docker/Dockerfile                   | 12 ++
  .github/workflows/ci.yml           | 8 +-

Committing... ✓
Pushing... ✓
Returning to develop... ✓

✓ Synced tooling to starter-template

Commit: abc1234 "Sync tooling from develop (2026-02-07)"
```

## When to Run

- After making improvements to skills, hooks, or rules
- After Docker/CI changes that benefit all users
- After fixing bugs in tooling scripts
- Periodically (weekly/monthly) to keep template current

## Manual Review Items

Some files need manual review before syncing:

### CLAUDE.md
Contains both general guidance and SDK-specific info. To sync just the tooling sections:
```bash
# Review diff first
git diff develop:CLAUDE.md starter-template:CLAUDE.md

# Or manually copy specific sections
```

### README.md
Has project status that differs between branches. Usually keep separate.

## Troubleshooting

### Merge conflicts
If starter-template has diverged:
```bash
git checkout starter-template
git reset --hard origin/starter-template
# Then run /backport again
```

### Missing directories
If a new directory was added on develop that doesn't exist on starter-template, the checkout command will create it.

### Reverting a bad sync
```bash
git checkout starter-template
git revert HEAD
git push origin starter-template
```

## Notes

- Always review changes before pushing (use `--dry-run` first)
- The sync is one-way: develop → starter-template
- Commit message includes date for tracking
- No cherry-picking complexity - just latest state
- Safe to run repeatedly
