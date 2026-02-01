#!/usr/bin/env python3
"""
Build searchable indexes from Classic Mac reference books.

Extracts:
- Critical tables (B-1, B-2, B-3, C-1, C-2, C-3)
- Function names and their locations
- Error codes and descriptions
- Chapter/section boundaries
"""

import json
import re
from pathlib import Path
from typing import Dict, List, Set

BOOKS_DIR = Path(__file__).parent.parent.parent / "books"
INDEX_DIR = Path(__file__).parent / "index"

# Known table locations (from doc-oracle.md)
TABLES = {
    "table_b1_moves_memory": {
        "book": "Inside_Macintosh_Volume_VI_1991.txt",
        "start": 223761,
        "end": 224228,
        "description": "Routines that MOVE/PURGE memory - DO NOT call at interrupt"
    },
    "table_b2_unsafe_no_move": {
        "book": "Inside_Macintosh_Volume_VI_1991.txt",
        "start": 224216,
        "end": 224391,
        "description": "Routines that don't move memory but still UNSAFE at interrupt"
    },
    "table_b3_interrupt_safe": {
        "book": "Inside_Macintosh_Volume_VI_1991.txt",
        "start": 224396,
        "end": 224607,
        "description": "Routines SAFE to call at interrupt time"
    },
    "table_c1_ot_hw_interrupt": {
        "book": "NetworkingOpenTransport.txt",
        "start": 43052,
        "end": 43451,
        "description": "OT functions callable at hardware interrupt time (all ISAs)"
    },
    "table_c2_ot_hw_interrupt_native": {
        "book": "NetworkingOpenTransport.txt",
        "start": 43502,
        "end": 43740,
        "description": "OT functions callable at hardware interrupt time (native ISA only)"
    },
    "table_c3_ot_deferred_task": {
        "book": "NetworkingOpenTransport.txt",
        "start": 43748,
        "end": 44144,
        "description": "OT functions callable from deferred tasks"
    }
}

def extract_table(book_path: Path, start: int, end: int) -> List[str]:
    """Extract function names from a table section."""

    # Common words that appear in tables but aren't functions
    COMMON_WORDS = {
        # Table column values
        'yes', 'no', 'n/a', 'None', 'asynchronous',
        # Table headers
        'Function', 'Calling', 'Needs', 'Atomic', 'restrictions', 'Native', 'Note',
        # Description words from multi-line table cells
        'only', 'foreground', 'background', 'task', 'calling', 'provide',
        'this', 'with', 'that', 'from', 'must', 'some', 'Some', 'other', 'factor',
        # Common English words
        'If', 'In', 'at', 'in', 'or', 'and', 'the', 'A', 'an', 'of', 'to', 'for',
        # Table formatting / appendix titles
        'Continued', 'Table', 'Volume', 'Special', 'Functions',
        # OCR fragments (too short to be real functions)
        'AOn', 'BOn', 'Exp', 'Leg',
    }

    functions = []
    with open(book_path, 'r', encoding='utf-8', errors='ignore') as f:
        for i, line in enumerate(f, 1):
            if i < start:
                continue
            if i > end:
                break

            line = line.strip()
            # Skip empty lines, headers, table markers
            if not line or line.startswith('Table') or line.startswith('—') or \
               line.startswith('Function') or line.startswith('Needs') or \
               line.startswith('Atomic') or line.startswith('Calling') or \
               len(line) < 3:
                continue

            # Extract function name (before any spaces, tabs, or special chars)
            # Handle entries like "GetLocalZones *" or "IPCListPorts +"
            func = re.split(r'[\s\*\+\‡†]+', line)[0]

            # Skip if empty after split
            if not func:
                continue

            # === COMPREHENSIVE JUNK FILTERING ===

            # 1. Basic sanity checks
            if func.isdigit():
                continue
            if len(func) < 3:  # Most Mac functions are 3+ chars
                continue

            # 2. Must start with uppercase letter (Mac API convention)
            # Exception: all-caps acronyms like RGB2CMY
            if not func[0].isupper():
                continue

            # 3. Must contain at least some alphanumeric characters
            if not any(c.isalnum() for c in func):
                continue

            # 4. OCR artifacts - starts with special characters
            if func[0] in ['[', ']', '{', '}', '|', '§', '(', ')', '=', '—']:
                continue

            # 5. OCR artifacts - ends with special characters
            if len(func) > 0 and func[-1] in ['!', '?', '|', '§', '=', '—']:
                continue

            # 6. Contains problematic characters (OCR errors)
            if any(c in func for c in ['|', '§', '†', '‡']):
                continue

            # 7. Starts with table markers
            if any(func.startswith(prefix) for prefix in ['Table', 'B-', 'A-', 'C-', 'D-', 'E-']):
                continue

            # 8. Common words exclusion list
            if func in COMMON_WORDS:
                continue

            # 9. All lowercase (descriptions, not function names)
            if func.islower():
                continue

            # 10. Numeric prefixes (like "1UCompPString" - keep these, they're real but odd)
            # They're actual Mac functions with version indicators

            # If it passes all filters, add it
            functions.append(func)

    return functions

def build_function_index() -> Dict:
    """Build index of all functions and their locations."""
    functions = {}

    # Add functions from tables with interrupt safety info
    for table_id, table_info in TABLES.items():
        book_path = BOOKS_DIR / table_info["book"]
        if not book_path.exists():
            continue

        table_functions = extract_table(book_path, table_info["start"], table_info["end"])

        # Determine interrupt safety from table type
        # Table B-3: Routines safe at interrupt time
        # Table C-1/C-2: OT functions callable at hardware interrupt time
        interrupt_safe = ("interrupt_safe" in table_id or
                          "hw_interrupt" in table_id or
                          table_id in ["table_c1_ot_hw_interrupt",
                                       "table_c2_ot_hw_interrupt_native"])
        moves_memory = "moves_memory" in table_id

        for func in table_functions:
            if func not in functions:
                functions[func] = {
                    "book": table_info["book"],
                    "lines": [],
                    "tables": [],
                    "interrupt_safe": None,
                    "moves_memory": None
                }

            functions[func]["tables"].append(table_id)
            if interrupt_safe:
                functions[func]["interrupt_safe"] = True
            if moves_memory:
                functions[func]["moves_memory"] = True
                functions[func]["interrupt_safe"] = False

    # Second pass: Detect functions in conflicting tables (sync vs async)
    # Per IM Vol VI Appendix B: Some routines have different behavior when
    # executed synchronously vs asynchronously and appear in multiple tables.
    for func, info in functions.items():
        tables = info["tables"]

        # Check if in both safe and unsafe tables
        has_safe = any(t in ["table_b3_interrupt_safe",
                            "table_c1_ot_hw_interrupt",
                            "table_c2_ot_hw_interrupt_native"] for t in tables)
        has_unsafe = any(t in ["table_b1_moves_memory",
                              "table_b2_unsafe_no_move"] for t in tables)

        if has_safe and has_unsafe:
            # Conflicting tables - safety depends on sync vs async execution
            info["interrupt_safe"] = None
            info["sync_async_dependent"] = True
            # Keep moves_memory flag if set (for sync version)

    return functions

def build_tables_index() -> Dict:
    """Build index of table contents."""
    tables_index = {}

    for table_id, table_info in TABLES.items():
        book_path = BOOKS_DIR / table_info["book"]
        if not book_path.exists():
            continue

        functions = extract_table(book_path, table_info["start"], table_info["end"])

        tables_index[table_id] = {
            "book": table_info["book"],
            "description": table_info["description"],
            "lines": [table_info["start"], table_info["end"]],
            "function_count": len(functions),
            "functions": sorted(functions)
        }

    return tables_index

def extract_error_codes() -> Dict:
    """Extract error codes from books."""
    error_codes = {
        "MacTCP": {},
        "OpenTransport": {},
        "AppleTalk": {}
    }

    # MacTCP errors (lines 5939-6120)
    mactcp_path = BOOKS_DIR / "MacTCP_Programmers_Guide_1989.txt"
    if mactcp_path.exists():
        with open(mactcp_path, 'r', encoding='utf-8', errors='ignore') as f:
            for i, line in enumerate(f, 1):
                if 5939 <= i <= 6120:
                    # Look for error names
                    match = re.search(r'(connection\w+|insufficient\w+|ip\w+Error)', line, re.IGNORECASE)
                    if match:
                        error_name = match.group(1)
                        error_codes["MacTCP"][error_name] = {
                            "line": i,
                            "book": "MacTCP_Programmers_Guide_1989.txt",
                            "context": line.strip()
                        }

    # Open Transport errors (Table B-1 around line 42307)
    ot_path = BOOKS_DIR / "NetworkingOpenTransport.txt"
    if ot_path.exists():
        with open(ot_path, 'r', encoding='utf-8', errors='ignore') as f:
            for i, line in enumerate(f, 1):
                # Look for kOT error codes
                match = re.search(r'(kOT\w+)', line)
                if match:
                    error_name = match.group(1)
                    if error_name not in error_codes["OpenTransport"]:
                        error_codes["OpenTransport"][error_name] = {
                            "line": i,
                            "book": "NetworkingOpenTransport.txt",
                            "context": line.strip()[:100]
                        }

    return error_codes

def build_keyword_index() -> Dict:
    """Build index of important keywords and concepts."""
    keywords = {
        "interrupt_safety": {
            "description": "Routines safe to call at interrupt time",
            "tables": ["table_b3_interrupt_safe", "table_c1_ot_hw_interrupt"],
            "books": ["Inside_Macintosh_Volume_VI_1991.txt", "NetworkingOpenTransport.txt"]
        },
        "memory_allocation": {
            "description": "Memory allocation at interrupt time",
            "keywords": ["NewPtr", "NewHandle", "OTAllocMem", "malloc"],
            "books": ["Inside_Macintosh_Volume_VI_1991.txt", "NetworkingOpenTransport.txt"]
        },
        "asr_callbacks": {
            "description": "MacTCP ASR callback restrictions",
            "lines": {"MacTCP_Programmers_Guide_1989.txt": [[2150, 2156], [4226, 4232]]},
            "books": ["MacTCP_Programmers_Guide_1989.txt"]
        },
        "ot_notifiers": {
            "description": "Open Transport notifier restrictions",
            "lines": {"NetworkingOpenTransport.txt": [[5793, 5826], [9143, 9148]]},
            "books": ["NetworkingOpenTransport.txt"]
        },
        "adsp_callbacks": {
            "description": "AppleTalk ADSP completion routine restrictions",
            "lines": {"Programming_With_AppleTalk_1991.txt": [[5924, 5926], [1554, 1558]]},
            "books": ["Programming_With_AppleTalk_1991.txt"]
        }
    }

    return keywords

def main():
    """Build all indexes."""
    INDEX_DIR.mkdir(parents=True, exist_ok=True)

    print("Building book indexes...")

    # Build function index
    print("  - Extracting functions from tables...")
    functions = build_function_index()
    with open(INDEX_DIR / "functions.json", 'w') as f:
        json.dump(functions, f, indent=2, sort_keys=True)
    print(f"    Found {len(functions)} functions")

    # Build tables index
    print("  - Building tables index...")
    tables = build_tables_index()
    with open(INDEX_DIR / "tables.json", 'w') as f:
        json.dump(tables, f, indent=2)
    print(f"    Indexed {len(tables)} tables")

    # Extract error codes
    print("  - Extracting error codes...")
    errors = extract_error_codes()
    with open(INDEX_DIR / "error_codes.json", 'w') as f:
        json.dump(errors, f, indent=2, sort_keys=True)
    total_errors = sum(len(v) for v in errors.values())
    print(f"    Found {total_errors} error codes")

    # Build keyword index
    print("  - Building keyword index...")
    keywords = build_keyword_index()
    with open(INDEX_DIR / "keywords.json", 'w') as f:
        json.dump(keywords, f, indent=2)
    print(f"    Indexed {len(keywords)} keyword categories")

    print("\nIndexes built successfully!")
    print(f"Output: {INDEX_DIR.absolute()}")

    # Print summary
    print("\n=== Summary ===")
    print(f"Functions indexed: {len(functions)}")
    print(f"Interrupt-safe functions: {sum(1 for f in functions.values() if f.get('interrupt_safe'))}")
    print(f"Tables extracted: {len(tables)}")
    print(f"Error codes: {total_errors}")
    print(f"  MacTCP: {len(errors['MacTCP'])}")
    print(f"  Open Transport: {len(errors['OpenTransport'])}")
    print(f"  AppleTalk: {len(errors['AppleTalk'])}")

if __name__ == "__main__":
    main()
