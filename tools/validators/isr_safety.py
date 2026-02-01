#!/usr/bin/env python3
"""ISR Safety Validator for Classic Mac callback code.

Scans MacTCP ASR, Open Transport notifier, and AppleTalk callback
functions for interrupt-time safety violations.

Usage:
    python tools/validators/isr_safety.py src/mactcp/
    python tools/validators/isr_safety.py src/mactcp/tcp_mactcp.c
    python tools/validators/isr_safety.py --check-content "code here"

Exit codes:
    0 - No violations found
    1 - Violations found
    2 - Error (file not found, etc.)
"""

import os
import re
import sys
from pathlib import Path
from dataclasses import dataclass
from typing import Optional

import click
from rich.console import Console
from rich.table import Table
from rich.panel import Panel

console = Console()


@dataclass
class ForbiddenCall:
    """A forbidden function call."""

    name: str
    category: str
    reason: str


@dataclass
class Violation:
    """A detected ISR safety violation."""

    file: str
    line: int
    callback_name: str
    forbidden_call: str
    category: str
    reason: str
    context: str  # Line of code


def load_forbidden_calls() -> dict[str, ForbiddenCall]:
    """Load forbidden calls from database file."""
    db_path = Path(__file__).parent / "forbidden_calls.txt"
    forbidden = {}

    if not db_path.exists():
        console.print(f"[red]Forbidden calls database not found: {db_path}[/red]")
        sys.exit(2)

    with open(db_path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue

            parts = line.split("|")
            if len(parts) != 3:
                continue

            name, category, reason = parts
            forbidden[name] = ForbiddenCall(name=name, category=category, reason=reason)

    return forbidden


def find_callback_functions(code: str) -> list[tuple[str, int, int]]:
    """Find callback function definitions in code.

    Returns list of (function_name, start_line, end_line).
    """
    callbacks = []

    # Patterns for callback functions
    patterns = [
        # Named patterns
        r"(?:static\s+)?(?:pascal\s+)?(?:void|OSErr)\s+(\w+_asr)\s*\(",
        r"(?:static\s+)?(?:pascal\s+)?(?:void|OSErr)\s+(\w+_notifier)\s*\(",
        r"(?:static\s+)?(?:pascal\s+)?(?:void|OSErr)\s+(\w+_completion)\s*\(",
        r"(?:static\s+)?(?:pascal\s+)?(?:void|OSErr)\s+(\w+_callback)\s*\(",
        r"(?:static\s+)?(?:pascal\s+)?(?:void|OSErr)\s+(\w+_event)\s*\(",
        # Pascal callback signatures (MacTCP ASR)
        r"(?:static\s+)?pascal\s+void\s+(\w+)\s*\(\s*StreamPtr",
        # OT notifier signature
        r"(?:static\s+)?pascal\s+void\s+(\w+)\s*\(\s*void\s*\*\s*\w*\s*,\s*OTEventCode",
        # ADSP completion signature
        r"(?:static\s+)?pascal\s+void\s+(\w+)\s*\(\s*DSPPBPtr",
        r"(?:static\s+)?pascal\s+void\s+(\w+)\s*\(\s*TPCCB",
    ]

    lines = code.split("\n")

    for pattern in patterns:
        for match in re.finditer(pattern, code, re.MULTILINE):
            func_name = match.group(1)
            # Find line number
            pos = match.start()
            line_num = code[:pos].count("\n") + 1

            # Find function end (matching braces)
            func_start = code.find("{", pos)
            if func_start == -1:
                continue

            brace_count = 1
            func_end = func_start + 1
            while brace_count > 0 and func_end < len(code):
                if code[func_end] == "{":
                    brace_count += 1
                elif code[func_end] == "}":
                    brace_count -= 1
                func_end += 1

            end_line = code[:func_end].count("\n") + 1
            callbacks.append((func_name, line_num, end_line))

    # Remove duplicates (same function matched by multiple patterns)
    seen = set()
    unique = []
    for cb in callbacks:
        if cb[0] not in seen:
            seen.add(cb[0])
            unique.append(cb)

    return unique


def check_function_for_violations(
    code: str,
    func_name: str,
    start_line: int,
    end_line: int,
    forbidden: dict[str, ForbiddenCall],
    filename: str,
) -> list[Violation]:
    """Check a callback function for forbidden calls."""
    violations = []
    lines = code.split("\n")

    # Extract function body
    func_lines = lines[start_line - 1 : end_line]
    func_body = "\n".join(func_lines)

    for call_name, call_info in forbidden.items():
        # Look for function calls (word boundary + parenthesis)
        pattern = rf"\b{re.escape(call_name)}\s*\("

        for i, line in enumerate(func_lines):
            # Skip comments
            line_stripped = line.split("//")[0]  # Remove line comments
            # Remove block comments (simple version)
            line_stripped = re.sub(r"/\*.*?\*/", "", line_stripped)

            if re.search(pattern, line_stripped):
                violations.append(
                    Violation(
                        file=filename,
                        line=start_line + i,
                        callback_name=func_name,
                        forbidden_call=call_name,
                        category=call_info.category,
                        reason=call_info.reason,
                        context=line.strip(),
                    )
                )

    return violations


def check_content(code: str, filename: str = "<stdin>") -> list[Violation]:
    """Check code content for ISR safety violations."""
    forbidden = load_forbidden_calls()
    all_violations = []

    # Find callback functions
    callbacks = find_callback_functions(code)

    for func_name, start_line, end_line in callbacks:
        violations = check_function_for_violations(
            code, func_name, start_line, end_line, forbidden, filename
        )
        all_violations.extend(violations)

    return all_violations


def check_file(filepath: Path) -> list[Violation]:
    """Check a single file for ISR safety violations."""
    try:
        code = filepath.read_text(encoding="utf-8", errors="replace")
        return check_content(code, str(filepath))
    except Exception as e:
        console.print(f"[red]Error reading {filepath}: {e}[/red]")
        return []


def check_directory(dirpath: Path) -> list[Violation]:
    """Check all C files in a directory for violations."""
    all_violations = []

    for filepath in dirpath.rglob("*.c"):
        violations = check_file(filepath)
        all_violations.extend(violations)

    return all_violations


def display_violations(violations: list[Violation]):
    """Display violations in a formatted table."""
    if not violations:
        console.print("[green]No ISR safety violations found.[/green]")
        return

    console.print(
        Panel(
            f"[bold red]Found {len(violations)} ISR Safety Violation(s)[/bold red]",
            title="ISR Safety Check",
        )
    )
    console.print()

    # Group by file
    by_file: dict[str, list[Violation]] = {}
    for v in violations:
        if v.file not in by_file:
            by_file[v.file] = []
        by_file[v.file].append(v)

    for filename, file_violations in by_file.items():
        console.print(f"[bold]{filename}[/bold]")

        table = Table(show_header=True, header_style="bold")
        table.add_column("Line", style="cyan", width=6)
        table.add_column("Callback", style="yellow", width=20)
        table.add_column("Forbidden Call", style="red", width=20)
        table.add_column("Reason", style="white")

        for v in sorted(file_violations, key=lambda x: x.line):
            table.add_row(str(v.line), v.callback_name, v.forbidden_call, v.reason)

        console.print(table)
        console.print()

    # Print fix suggestions
    console.print("[bold]Common Fixes:[/bold]")
    categories_seen = set(v.category for v in violations)

    if "memory" in categories_seen:
        console.print("  - Memory allocation: Use pre-allocated buffers")
    if "memory_ops" in categories_seen:
        console.print("  - memcpy/BlockMove: Use pt_memcpy_isr()")
    if "timing" in categories_seen:
        console.print("  - TickCount: Set timestamp=0, let main loop timestamp")
    if "sync_network" in categories_seen:
        console.print("  - Sync calls: Use async version with completion callback")
    if "io" in categories_seen:
        console.print("  - I/O: Set flags only, process in main loop")


@click.command()
@click.argument("target", required=False)
@click.option("--check-content", "-c", help="Check code content directly (for hooks)")
@click.option("--quiet", "-q", is_flag=True, help="Only output violations, no decoration")
def main(target: Optional[str], check_content: Optional[str], quiet: bool):
    """Check Classic Mac callback code for ISR safety violations.

    TARGET can be a file or directory to scan.
    """
    if check_content:
        # Check content directly (used by hooks)
        violations = globals()["check_content"](check_content)
    elif target:
        path = Path(target)
        if path.is_file():
            violations = check_file(path)
        elif path.is_dir():
            violations = check_directory(path)
        else:
            console.print(f"[red]Target not found: {target}[/red]")
            sys.exit(2)
    else:
        # Default to src/ directories for Mac code
        violations = []
        for subdir in ["mactcp", "opentransport", "appletalk"]:
            path = Path("src") / subdir
            if path.exists():
                violations.extend(check_directory(path))

        if not violations and not Path("src").exists():
            console.print("[yellow]No src/ directory found. Specify a target.[/yellow]")
            console.print("Usage: python tools/validators/isr_safety.py <file-or-directory>")
            sys.exit(0)

    if quiet:
        for v in violations:
            print(f"{v.file}:{v.line}: {v.forbidden_call} in {v.callback_name} - {v.reason}")
    else:
        display_violations(violations)

    # Exit with 1 if violations found
    sys.exit(1 if violations else 0)


if __name__ == "__main__":
    main()
