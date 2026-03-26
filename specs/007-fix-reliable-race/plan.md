# Implementation Plan: Fix test_reliable Responder Race

**Branch**: `007-fix-reliable-race` | **Date**: 2026-03-26 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/007-fix-reliable-race/spec.md`

## Summary

One-line fix in test_reliable.c: set g_moves_done after the responder's final send, not only on the initiator's final receive. Prevents re-discovery from flipping g_initiated after disconnect.

## Technical Context

**Language/Version**: C11 (test app)
**Primary Dependencies**: PeerTalk SDK
**Testing**: Hardware testing on Mac SE, Performa 6200, Performa 6400
**Target Platform**: Classic Mac (68k, PPC) + POSIX
**Project Type**: Test app fix
**Constraints**: Single file change (tests/test_reliable.c)
**Scale/Scope**: ~3 lines changed

## Constitution Check

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Three Apps Are the Spec | PASS | test_reliable exercises Chess pattern |
| VIII. Test Apps Prove the SDK | PASS | Fixing a test app to correctly prove the SDK |
| X. C89 for Portability | PASS | Test apps may use C11 |

## Project Structure

```text
tests/
└── test_reliable.c    # Single file changed
```

## Complexity Tracking

No violations. Single-line fix.
