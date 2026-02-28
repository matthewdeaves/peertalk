#!/usr/bin/env python3
"""ISR Safety Hook Helper - checks callback functions for forbidden calls.

Lightweight, zero-dependency helper for the ISR safety pre-commit hook.
Reads Claude Code hook JSON from stdin, reconstructs the full file content,
finds callback function boundaries, and checks ONLY within those boundaries
for forbidden calls.

This solves the false positive problem where OTBind/OTUnbind in normal init
code was flagged just because a notifier existed in the same file.

Exit codes:
    0 - No violations (or no callbacks found)
    1 - Violations found (outputs violation details to stdout)
"""

import json
import os
import re
import sys


# Callback function patterns (kept in sync with isr_safety.py)
CALLBACK_PATTERNS = [
    r"(?:static\s+)?(?:pascal\s+)?(?:void|OSErr)\s+(\w+_asr)\s*\(",
    r"(?:static\s+)?(?:pascal\s+)?(?:void|OSErr)\s+(\w+_notifier)\s*\(",
    r"(?:static\s+)?(?:pascal\s+)?(?:void|OSErr)\s+(\w+_completion)\s*\(",
    r"(?:static\s+)?(?:pascal\s+)?(?:void|OSErr)\s+(\w+_callback)\s*\(",
    r"(?:static\s+)?(?:pascal\s+)?(?:void|OSErr)\s+(\w+_event)\s*\(",
    r"(?:static\s+)?pascal\s+void\s+(\w+)\s*\(\s*StreamPtr",
    r"(?:static\s+)?pascal\s+void\s+(\w+)\s*\(\s*void\s*\*\s*\w*\s*,\s*OTEventCode",
    r"(?:static\s+)?pascal\s+void\s+(\w+)\s*\(\s*DSPPBPtr",
    r"(?:static\s+)?pascal\s+void\s+(\w+)\s*\(\s*TPCCB",
]


def find_callbacks(code):
    """Find callback functions and their line ranges.

    Returns list of (func_name, start_line, end_line) tuples.
    """
    callbacks = []
    seen = set()

    for pattern in CALLBACK_PATTERNS:
        for match in re.finditer(pattern, code, re.MULTILINE):
            func_name = match.group(1)
            if func_name in seen:
                continue
            seen.add(func_name)

            # Find opening brace of function body
            brace_pos = code.find("{", match.start())
            if brace_pos == -1:
                continue

            # Match braces to find function end
            depth = 1
            pos = brace_pos + 1
            while depth > 0 and pos < len(code):
                if code[pos] == "{":
                    depth += 1
                elif code[pos] == "}":
                    depth -= 1
                pos += 1

            start_line = code[:match.start()].count("\n") + 1
            end_line = code[:pos].count("\n") + 1
            callbacks.append((func_name, start_line, end_line))

    return callbacks


def load_forbidden(db_path):
    """Load forbidden calls database. Returns dict of name -> reason."""
    forbidden = {}

    if not os.path.exists(db_path):
        return forbidden

    with open(db_path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split("|")
            if len(parts) != 3:
                continue
            name, _category, reason = parts
            forbidden[name.strip()] = reason.strip()

    return forbidden


def strip_comments(text):
    """Remove C comments from text."""
    text = re.sub(r"//.*$", "", text, flags=re.MULTILINE)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return text


def check_callback_body(code, start_line, end_line, forbidden):
    """Check a callback function body for forbidden calls.

    Returns list of (call_name, reason) tuples.
    """
    violations = []
    lines = code.split("\n")
    body = "\n".join(lines[start_line - 1:end_line])
    clean_body = strip_comments(body)

    for call_name, reason in forbidden.items():
        pattern = rf"\b{re.escape(call_name)}\s*\("
        if re.search(pattern, clean_body):
            violations.append((call_name, reason))

    return violations


def get_full_content(tool_input):
    """Reconstruct full file content from hook tool input.

    For Write: content IS the full file.
    For Edit: read existing file and apply the edit.
    """
    file_path = tool_input.get("file_path", "")
    content_field = tool_input.get("content")
    new_string = tool_input.get("new_string")

    if content_field is not None:
        # Write tool - content is the full file
        return content_field

    if new_string is not None:
        # Edit tool - reconstruct full file by applying the edit
        old_string = tool_input.get("old_string", "")
        replace_all = tool_input.get("replace_all", False)
        try:
            with open(file_path) as f:
                content = f.read()
            if replace_all:
                content = content.replace(old_string, new_string)
            else:
                content = content.replace(old_string, new_string, 1)
            return content
        except FileNotFoundError:
            # New file being created via Edit (unusual but handle it)
            return new_string

    return ""


def main():
    try:
        hook_input = json.load(sys.stdin)
    except (json.JSONDecodeError, ValueError):
        sys.exit(0)

    tool_input = hook_input.get("tool_input", {})
    file_path = tool_input.get("file_path", "")

    # Only check Mac networking code
    if not any(p in file_path for p in ("mactcp", "opentransport", "appletalk")):
        sys.exit(0)

    content = get_full_content(tool_input)
    if not content:
        sys.exit(0)

    # Find callback functions
    callbacks = find_callbacks(content)
    if not callbacks:
        sys.exit(0)

    # Load forbidden calls database
    script_dir = os.path.dirname(os.path.abspath(__file__))
    db_path = os.path.join(script_dir, "forbidden_calls.txt")
    forbidden = load_forbidden(db_path)
    if not forbidden:
        sys.exit(0)

    # Check ONLY within callback function bodies
    all_violations = []
    for func_name, start, end in callbacks:
        violations = check_callback_body(content, start, end, forbidden)
        for call_name, reason in violations:
            all_violations.append((func_name, call_name, reason))

    if all_violations:
        for func_name, call_name, reason in all_violations:
            print(f"  - {call_name}: {reason} (in {func_name})")
        sys.exit(1)

    sys.exit(0)


if __name__ == "__main__":
    main()
