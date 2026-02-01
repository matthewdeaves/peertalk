# PeerTalk

Cross-platform peer-to-peer networking library for Classic Macintosh and modern systems.

## What It Is

PeerTalk is an SDK for peer-to-peer communication across:
- **POSIX** (Linux/macOS) - Reference implementation with automated testing
- **MacTCP** (System 6-7.5, 68k Macs) - Classic networking for older hardware
- **Open Transport** (System 7.6+, PPC Macs) - Modern Classic Mac networking
- **AppleTalk** (Any Mac with System 6+) - Local network discovery and messaging

The project demonstrates how to use Claude Code's extensibility features (skills, agents, hooks, rules) to implement a complex cross-platform SDK methodically.

## Current State

**Planning complete, implementation not started.**

- 12 phases planned (0-10 plus 3.5) with detailed specifications
- All Claude Code tooling in place (skills, agents, hooks, rules)
- Ready for implementation

Phase specifications are in `plan/`. The [blog post](https://matthewdeaves.com/blog/2026-01-11-rapid-prototyping-with-claude-code/) explains how they were created using Claude.

## Branches

- **`main`** - Will contain implemented SDK code as phases complete
- **`starter-template`** - Complete Claude Code customization example with all skills, agents, hooks, and plans but no SDK code. Use it to study the patterns or try implementing the planned phases yourself.

## Setup

```bash
git clone https://github.com/matthewdeaves/peertalk.git
cd peertalk

# For starter-template (recommended for learning):
git checkout starter-template

# Install minimal host dependencies
./tools/setup.sh           # Installs: jq, python3

# Build Docker image (all builds happen here)
./scripts/docker-build.sh  # ~2GB, contains entire toolchain
```

**Philosophy: All builds in Docker**
- **Host:** jq (hooks), python3 (MCP/validators), Docker
- **Docker:** Retro68, gcc, make, lcov, ctags, clang-format, everything else

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
| [docs/CLAUDE-CODE-SETUP.md](docs/CLAUDE-CODE-SETUP.md) | **Claude Code configuration guide** - skills, hooks, tools |
| [.claude/skills/](.claude/skills/) | 12 skills with progressive disclosure (main SKILL.md + references/, scripts/, assets/) |
| [.claude/mcp-servers/](.claude/mcp-servers/) | Classic Mac hardware FTP server for deploying binaries and fetching logs (SETUP.md, SUMMARY.md) |
| [.claude/hooks/](.claude/hooks/) | ISR safety, compile check, coverage, userFlags |
| [.claude/rules/](.claude/rules/) | [isr-safety](.claude/rules/isr-safety.md), [mactcp](.claude/rules/mactcp.md), [opentransport](.claude/rules/opentransport.md), [appletalk](.claude/rules/appletalk.md) |
| [plan/](plan/) | Phase specifications (0-10 plus 3.5) |
| [books/](books/) | Apple reference documentation |

## For Learners

If you want to understand how to customize Claude Code for domain-specific work:

1. Switch to `starter-template` branch
2. Read [STARTER-TEMPLATE.md](docs/STARTER-TEMPLATE.md) for an overview
3. Read [CLAUDE-CODE-SETUP.md](docs/CLAUDE-CODE-SETUP.md) for detailed documentation of all skills, hooks, and tools
4. Use `/session next` to find available work
5. Try implementing Phase 0 (POSIX-only, good starting point)

The tooling guides you through the implementation while enforcing safety rules automatically.
