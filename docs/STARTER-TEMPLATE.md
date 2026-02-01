# PeerTalk Starter Template

Complete Claude Code tooling (skills, agents, hooks) + detailed phase plans. **No SDK code** - use the tooling to build it yourself.

**Target audience:** Developers who want to implement the PeerTalk SDK using Claude Code's guided workflow, or study the tooling patterns for their own projects.

**Background:** Read [the blog post](https://matthewdeaves.com/blog/2026-01-11-rapid-prototyping-with-claude-code/) about how the phase plans were created.

## What's Here

```
.claude/
  skills/      # 14 skills with bundled resources (references/, scripts/, assets/)
  agents/      # 1 agent (cross-platform debug)
  mcp-servers/ # 1 MCP server (Classic Mac hardware via FTP + LaunchAPPL)
  hooks/       # 4 hooks: ISR safety (blocks), compile check, coverage, userFlags
  rules/       # Platform constraints with verified citations from Apple docs
plan/          # 12 detailed phase plans (0-10 plus 3.5)
books/         # Apple reference documentation (MacTCP guides, Inside Macintosh, etc.)
tools/         # ISR validator, book indexer
```

**The tooling guides you:** Use `/session next` to find work, `/implement` to write code from plans, `/build test` to verify. All skills, validation, and workflows are ready - just follow the phase plans to build the SDK.

## Setup

```bash
git clone https://github.com/matthewdeaves/peertalk.git
cd peertalk
git checkout starter-template
./tools/setup.sh  # Sets up: jq, python3, Docker image, MCP config
```

**Then restart Claude Code** to load MCP servers.

**All builds happen in Docker - no native toolchain needed on host.**

For detailed documentation of all skills, agents, hooks, and tools, see **[CLAUDE-CODE-SETUP.md](CLAUDE-CODE-SETUP.md)**.

## Try It

```bash
# Find next available work
/session next

# Implement a session - reads plan, writes code, verifies
/implement 1 1.0

# Search Classic Mac API documentation
/mac-api is TickCount safe at interrupt time?

# Build and test
/build test

# Mark done
/session complete 1.0
```

## What to Study

### Skills (`.claude/skills/`)

Each skill uses **progressive disclosure**: SKILL.md shows workflow overview (< 250 lines), details in `references/`, executable helpers in `scripts/`, templates in `assets/`. Study:

- **`/session`** - Parses plan files to track progress and find available work. `references/parsing-guide.md` has complete Grep examples.
- **`/implement`** - Orchestrates implementation: gathers context, verifies deliverables. `references/` has context-gathering, verification, patterns.
- **`/check-isr`** - Validates interrupt-safety rules in Classic Mac callback code
- **`/mac-api`** - Search authoritative Classic Mac reference books for API documentation, interrupt safety, error codes. Returns line-level citations from Inside Macintosh. Searches rules first (already verified), then indexed books, then raw book text. `references/` has book catalog, search strategy, key line references.
- **`/build`** - Build for all platforms with quality gates. Modes: quick (syntax), compile, test (POSIX + coverage), package (Mac binaries), release (full pipeline)
- **`/review`** - Review phase plans for implementability. Spawns 9 parallel subagents to verify APIs, check docs, validate dependencies
- **`/hw-test`** - Generate hardware test plans for manual testing on real Classic Mac hardware
- **`/backport`** - Identify tooling commits to cherry-pick to starter-template branch
- **`/setup-machine`** - Register new Classic Mac in machine registry, verify FTP connectivity, create directory structure
- **`/setup-launcher`** - Build and deploy LaunchAPPLServer and demo apps to registered Classic Mac
- **`/test-machine`** - Test FTP and LaunchAPPL connectivity to verify machine is ready for deployment
- **`/deploy`** - Deploy binaries to Classic Mac hardware via FTP
- **`/execute`** - Run apps remotely on Classic Mac via LaunchAPPL (tests without manual interaction)
- **`/fetch-logs`** - Retrieve PT_Log output from Classic Mac hardware, compare logs

### Agents (`.claude/agents/`)

Auto-triggered specialists:

- **`cross-platform-debug`** - Spawns when you say "works on Linux but crashes on Mac". Fetches logs from real hardware via MCP server, compares implementations, identifies divergence, reports fix with line numbers.

### MCP Server (`.claude/mcp-servers/`)

Model Context Protocol server for external integrations:

- **`classic-mac-hardware`** - Access to Classic Mac test machines via FTP and LaunchAPPL remote execution. Tools: deploy, execute, fetch-logs. Uses RumpusFTP (FTP) and LaunchAPPL (TCP port 1984). Follows Anthropic's code execution pattern for 98%+ token savings. See `SETUP.md` for configuration.

### Hooks (`.claude/hooks/`)

Hooks run automatically on file operations:

- **`isr-safety-check.sh`** - Pre-edit hook that **blocks** unsafe code in Mac callbacks
- **`quick-compile.sh`** - Post-edit hook that warns about syntax errors
- **`adsp-userflags.sh`** - Post-edit hook that warns about AppleTalk userFlags not being cleared
- **`coverage-check.sh`** - Post-command hook that reports test coverage

### Rules (`.claude/rules/`)

Platform constraints with verified citations:

- **`isr-safety.md`** - What you can/cannot do at interrupt time
- **`mactcp.md`** - MacTCP ASR rules, error codes, patterns
- **`opentransport.md`** - OT notifier rules, endpoint states, tilisten
- **`appletalk.md`** - ADSP callbacks, NBP discovery, userFlags clearing

Rules are loaded automatically based on file paths being edited.

## See Hooks in Action

Create Mac callback code with a forbidden call:

```c
static pascal void my_asr(StreamPtr stream, ...) {
    void *buf = malloc(1024);  // FORBIDDEN
}
```

The ISR safety hook **blocks the edit** and explains why:

```
BLOCKED: ISR Safety Violation
  - malloc: Dynamic allocation forbidden at interrupt time

Safe alternative: Pre-allocate buffers at init time
```

## See Skills in Action

### /session

Track project progress and find available work:

```
/session status    # Show overall completion
/session next      # Find next available session
/session blocked   # Show blocked sessions and dependencies
```

### /mac-api

Ask deep Classic Mac programming questions and get book-backed answers:

```
/mac-api is OTAllocMem safe at interrupt time?
/mac-api what registers must I preserve in MacTCP ASR?
/mac-api why does ADSP connection hang after attention message?
/mac-api difference between TCPNoCopyRcv and TCPRcv
/mac-api can I call Gestalt from interrupt time?
```

The skill:
1. Checks `.claude/rules/` for pre-verified answers with citations
2. Searches pre-built indexes (`tools/book_indexer/index/`) for functions, tables, error codes
3. Greps the raw books in `books/` when needed
4. Returns answers with exact line numbers you can verify

Example response:

```
## Answer

Yes, OTAllocMem is safe at hardware interrupt time, but it may return NULL.

## Source

**`.claude/rules/isr-safety.md`** - Table C-1 key entries

Already verified from Networking With Open Transport Table C-1 (Lines 43052-43451)

Note: Always check the return value - memory allocation can fail at interrupt
time even when the function itself is safe to call.
```

## Recommended Starting Point

**Phase 0 (PT_Log)** and **Phase 1 (Foundation)** are POSIX-only and good for learning the workflow without Mac complexity. Start here.

Once you reach **Phase 5 (MacTCP)**, you'll see the full system in action: `/mac-api` verifies APIs against the books, ISR safety hooks block unsafe callback code, and `/hw-test` generates hardware test plans.

## Key Patterns to Copy

1. **Progressive disclosure** - Main files < 250 lines (overview), details in `references/` (loaded on-demand)
2. **Skills read plan files directly** - No external tools needed, Claude parses markdown
3. **Skills have trust hierarchies** - `/mac-api` searches: Rules > indexes > raw books
4. **Hooks block or warn** - Pre-edit hooks can block, post-edit hooks warn
5. **Rules have verified citations** - `> **Verified:** Source (Lines X-Y)`
6. **Path-based rule loading** - Rules activate when editing matching paths

## Resources

| Location | Purpose |
|----------|---------|
| [CLAUDE.md](../CLAUDE.md) | Project quick reference |
| [Claude Code Setup](CLAUDE-CODE-SETUP.md) | Agents, skills, hooks, tools reference |
| [.claude/rules/](../.claude/rules/) | Platform rules |
| [.claude/skills/](../.claude/skills/) | Skill implementations |
| [plan/](../plan/) | Phase specifications |
| [Blog post](https://matthewdeaves.com/blog/2026-01-11-rapid-prototyping-with-claude-code/) | How the plans were created |
