#!/usr/bin/env python3
"""Aggregate all metrics into single JSON file"""

import json
import sys
import os
from datetime import datetime, timezone
from pathlib import Path

def main():
    if len(sys.argv) != 7:
        print("Usage: aggregate_metrics.py <test> <coverage> <isr> <quality> <ci> <output>")
        sys.exit(1)

    test_file, coverage_file, isr_file, quality_file, ci_file, output_file = sys.argv[1:]

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

        # Write output
        Path(output_file).write_text(json.dumps(aggregated, indent=2))
        print(f"✓ Aggregated metrics written to {output_file}")

    except Exception as e:
        print(f"Error aggregating metrics: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
