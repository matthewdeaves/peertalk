# Implementation Plan: Fix test_reliable Turn Deadlock

**Branch**: `004-fix-test-reliable` | **Date**: 2026-03-07 | **Spec**: `specs/004-fix-test-reliable/spec.md`
**Input**: Feature specification from `/specs/004-fix-test-reliable/spec.md`

## Summary

test_reliable deadlocks because both sides use `name[0] <= 'M'` to decide who goes first. When both are "Unnamed" (default on Classic Mac), both get `g_is_first = 0` and wait forever. Fix: use IP comparison via PT_PeerAddress in on_connected instead.

## Technical Context

**Language/Version**: C89/C90 (SDK), C11 (POSIX test apps)
**Testing**: Hardware test apps on real Classic Mac hardware + POSIX
**Target Platform**: POSIX, MacTCP (68k), Open Transport (PPC)
**Project Type**: Library test app fix
**Constraints**: C89 in test apps that run on Classic Mac, C11 allowed on POSIX-only

## Constitution Check

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Three Apps Are the Spec | PASS | test_reliable proves the Chess pattern |
| VIII. Test Apps Prove the SDK | PASS | Fixing a broken test app |
| X. C89 for Portability | PASS | strcmp is C89, PT_PeerAddress returns const char* |

All gates pass.

## Project Structure

```text
tests/test_reliable.c    # Only file changed
```

**Root cause**: Line 204 `g_is_first = (name[0] <= 'M')` — both sides get "Unnamed", 'U' > 'M', both think they're second.

**Fix**: In on_connected, compare `PT_PeerAddress(peer)` (the remote peer's IP) with our own local IP. The side with the lower IP goes first. Use strcmp on dotted-quad strings — this works because the IPs are on the same /24 subnet (10.188.1.x).
