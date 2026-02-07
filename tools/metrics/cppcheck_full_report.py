#!/usr/bin/env python3
"""Parse cppcheck XML output and print a summary."""

import xml.etree.ElementTree as ET
import sys

def main():
    xml_file = sys.argv[1] if len(sys.argv) > 1 else "build/analysis/cppcheck-full.xml"

    try:
        tree = ET.parse(xml_file)
        root = tree.getroot()
    except Exception as e:
        print(f"Failed to parse XML: {e}")
        sys.exit(0)

    severity_counts = {}
    issues = []

    errors = root.find("errors")
    if errors is not None:
        for error in errors.findall("error"):
            sev = error.get("severity", "unknown")
            severity_counts[sev] = severity_counts.get(sev, 0) + 1

            loc = error.find("location")
            file_path = loc.get("file", "") if loc is not None else ""
            line = loc.get("line", "0") if loc is not None else "0"

            issues.append({
                "severity": sev,
                "id": error.get("id", ""),
                "msg": error.get("msg", ""),
                "file": file_path,
                "line": line
            })

    print("=" * 60)
    print("CPPCHECK FULL ANALYSIS (--force)")
    print("=" * 60)
    print()
    print("Summary by severity:")
    for sev in ["error", "warning", "style", "performance", "portability", "information"]:
        if sev in severity_counts:
            print(f"  {sev}: {severity_counts[sev]}")
    print(f"  TOTAL: {len(issues)}")
    print()

    critical = [i for i in issues if i["severity"] in ("error", "warning")]
    if critical:
        print("Errors and Warnings:")
        print("-" * 60)
        for i in critical[:30]:
            print(f"[{i['severity'].upper()}] {i['file']}:{i['line']}")
            print(f"  {i['id']}: {i['msg'][:100]}")
            print()
        if len(critical) > 30:
            print(f"... and {len(critical) - 30} more")
    else:
        print("No errors or warnings found.")

if __name__ == "__main__":
    main()
