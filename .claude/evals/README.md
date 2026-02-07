# Skill Evaluation Test Cases

This directory contains test case definitions for validating Claude Code skill behavior.

## Purpose

Each JSON file defines expected behavior for a skill, helping ensure skills work correctly after changes.

## Format

```json
{
  "skills": ["skill-name"],
  "query": "User query that triggers the skill",
  "files": ["relevant", "files"],
  "expected_behavior": [
    "First expected behavior",
    "Second expected behavior"
  ]
}
```

## Test Cases

| File | Skill | Tests |
|------|-------|-------|
| `build-test.json` | `/build` | Test compilation and coverage |
| `check-isr-validate.json` | `/check-isr` | ISR safety validation |
| `implement-session.json` | `/implement` | Session implementation workflow |
| `review-plan.json` | `/review` | Plan review with subagents |
| `session-next.json` | `/session next` | Finding next available session |
| `session-status.json` | `/session status` | Project progress display |

## Usage

These are reference definitions, not automated tests. Use them to:
1. Verify skill behavior after modifications
2. Document expected skill outputs
3. Guide manual testing of skills

## Adding New Evals

When creating a new skill, add an eval file:
1. Create `{skill-name}-{scenario}.json`
2. Define the triggering query
3. List expected behaviors
4. Test manually to verify
