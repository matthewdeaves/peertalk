# PeerTalk

Cross-platform peer-to-peer networking library for Classic Macintosh and modern systems.

[![Metrics Dashboard](https://img.shields.io/badge/metrics-dashboard-blue)](https://matthewdeaves.github.io/peertalk/) [![Coverage](https://img.shields.io/badge/coverage-40.9%25-yellow)](https://matthewdeaves.github.io/peertalk/coverage/)

## What It Is

PeerTalk is an SDK for peer-to-peer communication across:
- **POSIX** (Linux/macOS) - Reference implementation with automated testing
- **MacTCP** (System 6-7.5, 68k Macs) - Classic networking for older hardware
- **Open Transport** (System 7.6+, PPC Macs) - Modern Classic Mac networking
- **AppleTalk** (System 6+) - Mac-to-Mac only (POSIX bridging requires Phase 10)

The project demonstrates how to use [Claude Code](https://docs.anthropic.com/en/docs/claude-code)'s extensibility features (skills, agents, hooks, rules) to implement a complex cross-platform SDK methodically.

## Current State

**Planning complete, implementation not started.**

- 12 phases planned (0-10 plus 3.5) with detailed specifications
- All Claude Code tooling in place (skills, agents, hooks, rules)
- Ready for implementation

Phase specifications are in `plan/`. The [blog post](https://matthewdeaves.com/blog/2026-01-11-rapid-prototyping-with-claude-code/) explains how they were created using Claude.

## Branches

- **`main`** - Will contain implemented SDK code as phases complete
- **`starter-template`** - Complete tooling + plans, no SDK code. Use Claude's skills/agents/hooks to build the SDK yourself by following the phase plans.

## Setup

```bash
git clone https://github.com/matthewdeaves/peertalk.git
cd peertalk

# For starter-template (recommended for learning):
git checkout starter-template

# ONE command to set up everything:
./tools/setup.sh
```

This sets up:
- ✓ Host dependencies (jq, python3)
- ✓ Docker environment (~2GB, contains entire toolchain)
- ✓ MCP server configuration

**Then restart Claude Code** to load MCP servers.

**Philosophy: All builds in Docker**
- **Host:** jq (hooks), python3 (MCP/validators), [Docker](https://docs.docker.com/get-docker/)
- **Docker:** [Retro68](https://github.com/autc04/Retro68), gcc, make, lcov, ctags, clang-format, everything else

## Usage

Launch Claude Code and use skills to guide implementation:

```
/session              # Track progress, find available work from the plan folder
/implement            # Implement a session from the plan folder
/build test           # Compile and run POSIX tests to verify implementation
/mac-api [query]      # Search Inside Macintosh books for API documentation
```

## Structure

| Location | Contents |
|----------|----------|
| [docs/CLAUDE-CODE-SETUP.md](docs/CLAUDE-CODE-SETUP.md) | **Complete guide** to all Claude Code customizations: skills, hooks, MCP servers, rules |
| [.claude/skills/](.claude/skills/) | **12 custom commands** like `/build`, `/deploy`, `/execute` - each with docs, scripts, and assets |
| [.claude/mcp-servers/](.claude/mcp-servers/) | **MCP server** connecting Claude to real Classic Mac hardware via FTP/[LaunchAPPL](https://github.com/autc04/Retro68?tab=readme-ov-file#launchappl) (deploy, execute, fetch-logs) |
| [.claude/hooks/](.claude/hooks/) | **Automated checks** that run on file save: ISR safety validation, compile checks, coverage reports |
| [.claude/rules/](.claude/rules/) | **Platform-specific rules** auto-loaded when editing Mac code: ISR safety, MacTCP, Open Transport, AppleTalk |
| [plan/](plan/) | **Implementation plans** for 12 phases (0-10 plus 3.5) with sessions, tasks, and acceptance criteria |
| [books/](books/) | **Apple reference docs**: Inside Macintosh, MacTCP Guide, Open Transport, AppleTalk manuals (used by `/mac-api`) |

## For Learners

If you want to understand how to customize Claude Code for domain-specific work:

1. Switch to `starter-template` branch
2. Read [STARTER-TEMPLATE.md](docs/STARTER-TEMPLATE.md) for an overview
3. Read [CLAUDE-CODE-SETUP.md](docs/CLAUDE-CODE-SETUP.md) for detailed documentation of all skills, hooks, and tools
4. Use `/session next` to find available work
5. Try implementing Phase 0 (POSIX-only, good starting point)

The tooling guides you through the implementation while enforcing safety rules automatically.
