# Claude Code Configuration

Custom skills, hooks, and rules for PeerTalk development. These extend Claude Code with project-specific automation and domain knowledge.

## Quick Start

```bash
/session next      # Find next unblocked session based on phase dependencies
/implement 1 1.2   # Implement phase 1, session 1.2 - gathers context, writes code, verifies
/build test        # POSIX build with tests and coverage report
/check-isr         # Scan Mac callback code for interrupt-safety violations
/mac-api TickCount # Search Inside Macintosh books for API documentation
```

## Skills

User-invocable commands (slash commands). Use **progressive disclosure** (main file loads first, `references/` on-demand).

| Skill | What It Does | File | Bundled Resources |
|-------|--------------|------|-------------------|
| /session | Parse `plan/PHASE-*.md` files, track completion, find next available work respecting phase dependencies | [SKILL.md](../.claude/skills/session/SKILL.md) | parsing-guide (references) |
| /implement | Full implementation workflow: gather platform rules, write code using `/mac-api` for Classic Mac API verification, verify against acceptance criteria | [SKILL.md](../.claude/skills/implement/SKILL.md) | context-gathering, verification, patterns (references) |
| /build | Calls `build_all.sh` and `quality_gates.sh`. Build for POSIX, 68k (MacTCP), or PPC (Open Transport). Modes: quick, compile, test, package, release | [SKILL.md](../.claude/skills/build/SKILL.md) | check-build-prereqs.sh (scripts) |
| /check-isr | Calls `isr_safety.py`. Scan callback functions for forbidden calls (malloc, TickCount, sync network calls). Catches bugs that crash on real hardware | [SKILL.md](../.claude/skills/check-isr/SKILL.md) | - |
| /hw-test | Generate test checklists for manual testing on real Classic Mac hardware. Track pass/fail by platform | [SKILL.md](../.claude/skills/hw-test/SKILL.md) | test_plan_template.md (assets) |
| /review | Spawns 9 parallel subagents (Explore, general-purpose) to check APIs exist, docs are accurate, dependencies are met. Uses `/mac-api` for Classic Mac documentation verification. Project-specific version of [implementable](https://github.com/matthewdeaves/claude-code-prompts/tree/master/skills/implementable) | [SKILL.md](../.claude/skills/review/SKILL.md) | subagent-prompts, synthesis-format, auto-apply-rules (references) |
| /backport | Analyze git commits for tooling improvements to cherry-pick to starter-template branch | [SKILL.md](../.claude/skills/backport/SKILL.md) | - |
| /mac-api | Search authoritative Classic Mac reference books for API documentation, interrupt safety rules, error codes. Returns line-level citations from Inside Macintosh, MacTCP Guide, Open Transport docs | [SKILL.md](../.claude/skills/mac-api/SKILL.md) | book-catalog, search-strategy, key-line-references (references) |
| /setup-machine | Register new Classic Mac in machine registry. Collects machine details, verifies FTP connectivity, creates directory structure. First step in hardware setup | [SKILL.md](../.claude/skills/setup-machine/SKILL.md) | - |
| /setup-launcher | Build and deploy LaunchAPPLServer to registered Classic Mac. Builds platform-specific binary using Retro68, deploys via FTP. Second step in hardware setup | [SKILL.md](../.claude/skills/setup-launcher/SKILL.md) | - |
| /test-machine | Test FTP and LaunchAPPL connectivity to a Classic Mac. Simple wrapper around MCP test_connection tool with clear error messages | [SKILL.md](../.claude/skills/test-machine/SKILL.md) | - |
| /deploy | Deploy compiled binaries to Classic Mac test machines via FTP. Uses classic-mac-hardware MCP server. Supports single machine, platform, or all machines | [SKILL.md](../.claude/skills/deploy/SKILL.md) | - |
| /execute | Execute applications on Classic Mac via LaunchAPPL remote execution. Uses MCP execute_binary tool. Tests apps without manual interaction | [SKILL.md](../.claude/skills/execute/SKILL.md) | - |
| /fetch-logs | Retrieve PT_Log output from Classic Mac test machines via FTP. Uses classic-mac-hardware MCP server. Supports fetching from single machine, platform, or all. Optional side-by-side comparison | [SKILL.md](../.claude/skills/fetch-logs/SKILL.md) | - |

## Hooks

Shell scripts triggered before/after tool use. Configured in [settings.json](../.claude/settings.json).

| Hook | Trigger | What It Catches |
|------|---------|-----------------|
| [isr-safety-check.sh](../.claude/hooks/isr-safety-check.sh) | PreToolUse (Edit/Write) | **Blocks** edits containing malloc, TickCount, sync network calls in Mac callback code. Prevents bugs that only crash on real hardware |
| [quick-compile.sh](../.claude/hooks/quick-compile.sh) | PostToolUse (Edit/Write) | Runs fast syntax check via Retro68 Docker. Catches compile errors immediately after edits |
| [adsp-userflags.sh](../.claude/hooks/adsp-userflags.sh) | PostToolUse (Edit/Write) | Warns when ADSP userFlags accessed without clearing. Forgetting this hangs connections (verified: Programming With AppleTalk Lines 5780-5782) |
| [coverage-check.sh](../.claude/hooks/coverage-check.sh) | PostToolUse (Bash) | Reports test coverage after test runs. Warns if below 10% threshold |

## Rules

Platform-specific knowledge loaded automatically when editing files in matching directories. Each rule contains verified citations from Apple documentation.

| Rule | Auto-loads for | Contains | File |
|------|----------------|----------|------|
| MacTCP | `src/mactcp/**` | ASR callback rules, TCPPassiveOpen gotchas, error codes, buffer patterns. Verified from MacTCP Programmer's Guide | [mactcp.md](../.claude/rules/mactcp.md) |
| Open Transport | `src/opentransport/**` | Notifier rules, endpoint states, tilisten pattern, Table C-1 (interrupt-safe functions). Verified from Networking With Open Transport | [opentransport.md](../.claude/rules/opentransport.md) |
| AppleTalk | `src/appletalk/**` | ADSP callbacks, NBP discovery limits, critical userFlags clearing requirement. Verified from Programming With AppleTalk | [appletalk.md](../.claude/rules/appletalk.md) |
| ISR Safety | Cross-platform | What you can/cannot do at interrupt time. Table B-3 safe routines, register preservation, deferred task patterns. Verified from Inside Macintosh Volume VI | [isr-safety.md](../.claude/rules/isr-safety.md) |

## Tools

Python validators and build scripts used by skills and hooks.

| Tool | Used By | File |
|------|---------|------|
| ISR validator | `/check-isr` skill, `isr-safety-check.sh` hook. Parses C code to find forbidden function calls in callback contexts | [isr_safety.py](../tools/validators/isr_safety.py) |
| Forbidden calls | ISR validator. 130+ functions categorized: memory, timing, I/O, sync network, toolbox | [forbidden_calls.txt](../tools/validators/forbidden_calls.txt) |
| Build script | `/build` skill. Builds POSIX natively, 68k/PPC via Retro68 Docker container | [build_all.sh](../tools/build/build_all.sh) |
| Quality gates | `/build` skill. Enforces max function length (100 lines), file size (500 lines), coverage (10%) | [quality_gates.sh](../tools/build/quality_gates.sh) |

## MCP Servers

Model Context Protocol servers for external integrations.

| Server | Purpose | File | Configuration |
|--------|---------|------|---------------|
| classic-mac-hardware | Access to Classic Mac test machines via FTP (RumpusFTP) and LaunchAPPL remote execution (TCP port 1984). Tools: deploy_binary, execute_binary, fetch_logs, upload_file, download_file. Supports relative paths from /workspace | [server.py](../.claude/mcp-servers/classic-mac-hardware/server.py) | [SETUP.md](../.claude/mcp-servers/classic-mac-hardware/SETUP.md) |

**Design:** Follows [Anthropic's code execution pattern](https://www.anthropic.com/engineering/code-execution-with-mcp) for 98%+ token savings.

## Agents

Auto-triggered specialists spawned based on conversation context.

| Agent | Auto-Triggers | Purpose | File |
|-------|---------------|---------|------|
| cross-platform-debug | "Works on Linux but crashes on SE/30", "Different behavior on Mac vs POSIX" | Compares implementations, fetches logs from real hardware via MCP, checks ISR safety/byte order/alignment, reports fix with line numbers | [cross-platform-debug.md](../.claude/agents/cross-platform-debug.md) |

## Structure

```
.claude/
├── settings.json          # Hook configuration + permissions
├── skills/                # Slash commands (14 skills)
│   └── <name>/
│       ├── SKILL.md       # Main skill definition
│       ├── references/    # On-demand documentation
│       ├── scripts/       # Executable helpers
│       └── assets/        # Templates/files
├── agents/                # Auto-triggered specialists (1 agent)
│   └── <name>.md          # Agent definition
├── mcp-servers/           # MCP servers (1 server)
│   └── <name>/
│       ├── server.py      # MCP server implementation
│       ├── README.md      # Documentation
│       └── SETUP.md       # Configuration guide
├── hooks/                 # Pre/post tool scripts (4 hooks)
├── rules/                 # Auto-loaded platform rules (4 rules)
└── logs/                  # Hook execution logs
tools/
├── validators/            # Python validators
├── book_indexer/          # Index generator for Classic Mac books
└── build/                 # Build scripts
```

## Prerequisites

Run the setup script to install minimal dependencies:

```bash
./tools/setup.sh
```

**Requirements:**
- **jq** - Hooks parse JSON (e.g., `.claude/hooks/isr-safety-check.sh`)
- **python3** - MCP server + validators run on host
- **Docker** - ALL builds happen in Docker (POSIX + Mac)

**What's in Docker:**
- Retro68 (Mac cross-compiler for 68k and PPC)
- gcc, make (POSIX builds)
- lcov (coverage), ctags (quality gates), clang-format
- All dependencies pre-installed

Hooks use Docker for compilation - no native build tools needed on host.

## Adding New Components

- **Skill**: Add `.claude/skills/<name>/SKILL.md` with frontmatter. Keep main file < 250 lines, extract to `references/`, `scripts/`, `assets/`
- **Hook**: Add `.claude/hooks/<name>.sh`, update `.claude/settings.json`
- **Rule**: Add `.claude/rules/<name>.md`, configure path matching

**Progressive disclosure:** Main files load first (overview, workflow). Reference files load on-demand (details, examples, templates).

See [Claude Code documentation](https://docs.anthropic.com/en/docs/claude-code) for details.
