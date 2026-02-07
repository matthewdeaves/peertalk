#!/usr/bin/env python3
"""Aggregate all metrics into single JSON file"""

import json
import sys
import os
from datetime import datetime, timezone
from pathlib import Path

def main():
    if len(sys.argv) < 7 or len(sys.argv) > 10:
        print("Usage: aggregate_metrics.py <test> <coverage> <isr> <quality> <ci> <output> [binary_size] [complexity] [test_count]")
        sys.exit(1)

    test_file = sys.argv[1]
    coverage_file = sys.argv[2]
    isr_file = sys.argv[3]
    quality_file = sys.argv[4]
    ci_file = sys.argv[5]
    output_file = sys.argv[6]
    binary_size_file = sys.argv[7] if len(sys.argv) > 7 else None
    complexity_file = sys.argv[8] if len(sys.argv) > 8 else None
    test_count_file = sys.argv[9] if len(sys.argv) > 9 else None

    try:
        # Load all metrics
        test_data = json.loads(Path(test_file).read_text())
        coverage_data = json.loads(Path(coverage_file).read_text())
        isr_data = json.loads(Path(isr_file).read_text())
        quality_data = json.loads(Path(quality_file).read_text())
        ci_data = json.loads(Path(ci_file).read_text())

        # Aggregate
        aggregated = {
            "timestamp": datetime.now(timezone.utc).isoformat().replace('+00:00', 'Z'),
            "commit": os.environ.get("GITHUB_SHA", "unknown")[:7],
            "branch": os.environ.get("GITHUB_REF_NAME", "unknown"),
            "test_results": test_data,
            "coverage": coverage_data,
            "isr_safety": isr_data,
            "quality": quality_data,
            "ci_metrics": ci_data
        }

        # Add optional metrics if provided
        if binary_size_file and Path(binary_size_file).exists():
            aggregated["binary_size"] = json.loads(Path(binary_size_file).read_text())

        if complexity_file and Path(complexity_file).exists():
            aggregated["complexity"] = json.loads(Path(complexity_file).read_text())

        if test_count_file and Path(test_count_file).exists():
            aggregated["test_count"] = json.loads(Path(test_count_file).read_text())

        # Write output
        Path(output_file).write_text(json.dumps(aggregated, indent=2))
        print(f"✓ Aggregated metrics written to {output_file}")

    except Exception as e:
        print(f"Error aggregating metrics: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
