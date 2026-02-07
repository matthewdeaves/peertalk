# GitHub Workflow Best Practices

Rules for writing clean, maintainable GitHub Actions workflows.

## No Inline Scripts

**NEVER embed complex scripts directly in workflow YAML files.**

Bad - inline heredoc/script:
```yaml
- name: Process data
  run: |
    python3 << EOF
    import json
    # 50 lines of Python...
    EOF
```

Good - external script:
```yaml
- name: Process data
  run: python3 tools/my_script.py
```

## Why?

1. **GitHub parsing issues** - Complex nested quotes/heredocs can break workflow parsing
2. **Testability** - External scripts can be tested locally
3. **Readability** - Workflows stay focused on orchestration, not implementation
4. **Reusability** - Scripts can be called from multiple workflows or locally
5. **Syntax highlighting** - Proper IDE support for the script language

## Guidelines

1. **Keep `run:` blocks simple** - One-liners or a few shell commands max
2. **Use external scripts** for anything over 5-10 lines
3. **Place scripts in `tools/`** or appropriate subdirectory
4. **Make scripts executable** (`chmod +x`)
5. **Use proper shebang** (`#!/bin/bash`, `#!/usr/bin/env python3`)

## Example Structure

```
.github/workflows/
  ci.yml              # Orchestration only
  nightly.yml         # Orchestration only
tools/
  metrics/
    extract_coverage.sh      # Called by CI
    cppcheck_full_report.py  # Called by nightly
  validators/
    isr_safety.py           # Called by CI
```

## Workflow Complexity

If a workflow step requires:
- Loops or conditionals → external script
- Multiple tools/commands → external script
- Data processing → external script
- Error handling beyond `|| true` → external script
