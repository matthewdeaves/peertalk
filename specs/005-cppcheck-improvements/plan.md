# Implementation Plan: Cppcheck Code Quality Improvements

**Branch**: `005-cppcheck-improvements` | **Date**: 2026-03-07 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/005-cppcheck-improvements/spec.md`

## Summary

Fix 18 code quality issues identified by cppcheck static analysis:
- 1 unsigned comparison bug (dead code)
- 9 const variable pointer improvements
- 3 const parameter pointer improvements
- 2 callback const parameter evaluations
- 3 variable scope reductions

All changes are localized edits to existing source files with no new files or architectural changes.

## Technical Context

**Language/Version**: C89/C90 (SDK code)
**Primary Dependencies**: None (internal code changes only)
**Storage**: N/A
**Testing**: Existing test suite + cppcheck verification
**Target Platform**: POSIX, MacTCP (68k/PPC), Open Transport (68k/PPC)
**Project Type**: Library (SDK)
**Performance Goals**: N/A (code quality, no performance impact)
**Constraints**: Must maintain C89 compliance, must compile on all 5 platform targets
**Scale/Scope**: 4 source files, 18 localized changes

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

- [x] Does this serve Bomberman, Chess, or Chat? (I) - Yes, improves SDK quality for all apps
- [x] Am I adding config knobs or options? (IV) - No
- [x] Does this allocate after init? (V) - No
- [x] Is this C89-clean in SDK code? (X) - Yes, all changes maintain C89 compliance

## Project Structure

### Documentation (this feature)

```text
specs/005-cppcheck-improvements/
├── plan.md              # This file
├── spec.md              # Feature specification
├── checklists/
│   └── requirements.md  # Specification quality checklist
└── tasks.md             # Task list (to be generated)
```

### Source Code (files to modify)

```text
src/
├── core/
│   └── pt_core.c        # 6 changes (1 bug fix, 4 const vars, 1 const param)
├── platform/
│   ├── posix/
│   │   └── pt_posix.c   # 4 changes (1 const var, 1 callback eval, 2 scope)
│   ├── mactcp/
│   │   └── pt_mactcp.c  # 6 changes (3 const vars, 1 const param, 1 callback eval, 1 scope)
│   └── opentransport/
│       └── pt_ot.c      # 1 change (1 const param)
```

**Structure Decision**: No structural changes. All modifications are in-place edits to existing source files.

## Complexity Tracking

No violations - this is a simple code quality improvement with no architectural changes.
