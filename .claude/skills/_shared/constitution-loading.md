# Constitution Loading

The constitution is the root decision authority for all review and implementation decisions. Load it before making any judgment calls.

## Search Order

Check these locations in order, use the first one found:

1. `plan/constitution.md`
2. `.specify/memory/constitution.md`
3. `./constitution.md`
4. Fallback: synthesize from project files (see below)
5. Last resort: hardcoded defaults

## Fallback Synthesis

If no constitution.md exists, synthesize decision principles from:

1. **`plan/PROJECT_GOALS.md`** - Extract stated goals and priorities
2. **`CLAUDE.md`** - Extract quality gates, platform requirements, protocol constants
3. **`.claude/rules/`** - Extract safety rules (ISR, build, hardware)

Combine into an implicit constitution for decision-making.

## Last Resort Defaults

If no project files provide guidance, use this hierarchy:

1. **Safety** - ISR-safety, memory safety, crash prevention
2. **Goals** - Match stated project objectives
3. **Simplicity** - Fewer moving parts, less indirection
4. **Performance** - Cache efficiency, memory bandwidth
5. **Debuggability** - Better logging, clearer state machines

## Usage in Skills

### In /review

- Load constitution before synthesis
- Use constitution principles to evaluate design decisions
- Citation format: `[Constitution: "principle text"]`
- Check spec/plan/tasks decisions against constitution principles
- Flag contradictions as "Constitution Alignment Issue"

### In /implement

- Load constitution before implementation
- Use constitution to resolve ambiguous implementation choices
- When multiple valid approaches exist, pick the one that best aligns with constitution
- Citation format: `[Constitution: "principle text"]`

## Constitution Format Reference

A well-formed constitution.md contains:

```markdown
# Constitution

## Core Principles
1. Principle one...
2. Principle two...

## Decision Priorities
When principles conflict, resolve in this order:
1. Highest priority principle
2. Next priority...

## Non-Negotiables
- Things that must always be true...

## Explicitly Out of Scope
- Things we will not do...
```

## CRITICAL Rules

- **NEVER auto-modify constitution.md** - Only the user changes the constitution
- **Constitution overrides hardcoded defaults** - If constitution says "performance over simplicity", follow that
- **Absence is not guidance** - If the constitution is silent on a topic, fall back to project files then defaults
- **Quote the constitution** - When a decision is informed by a constitution principle, cite it
