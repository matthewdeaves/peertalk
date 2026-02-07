#!/usr/bin/env python3
"""Aggregate all metrics into single JSON file for the dashboard.

This script collects metrics from multiple sources and combines them
into a single JSON file that is uploaded to GitHub Pages.

Required inputs:
  - test_results.json: Test pass/fail counts
  - coverage.json: Code coverage percentages
  - isr_safety.json: ISR safety violations
  - quality.json: Code quality metrics
  - ci_metrics.json: CI timing metrics

Optional inputs (looked up from metrics_dir):
  - binary_size.json: Library/binary sizes
  - complexity.json: Cyclomatic complexity
  - test_count.json: Test counts by file
  - cppcheck.json: Static analysis results
  - duplicates.json: Code duplication metrics
"""

import json
import sys
import os
from datetime import datetime, timezone
from pathlib import Path


def load_json_safe(path: Path) -> dict:
    """Load JSON file, returning empty dict if not found or invalid."""
    try:
        if path.exists():
            return json.loads(path.read_text())
    except (json.JSONDecodeError, IOError) as e:
        print(f"Warning: Could not load {path}: {e}", file=sys.stderr)
    return {}


def main():
    if len(sys.argv) < 7:
        print("Usage: aggregate_metrics.py <test> <coverage> <isr> <quality> <ci> <output> [metrics_dir]")
        print("")
        print("If metrics_dir is provided, optional metrics are loaded from that directory.")
        sys.exit(1)

    test_file = Path(sys.argv[1])
    coverage_file = Path(sys.argv[2])
    isr_file = Path(sys.argv[3])
    quality_file = Path(sys.argv[4])
    ci_file = Path(sys.argv[5])
    output_file = Path(sys.argv[6])

    # Optional: directory containing additional metric files
    metrics_dir = Path(sys.argv[7]) if len(sys.argv) > 7 else None

    # Legacy support: individual file arguments
    if metrics_dir and metrics_dir.suffix == '.json':
        # Old-style invocation with individual files
        binary_size_file = Path(sys.argv[7]) if len(sys.argv) > 7 else None
        complexity_file = Path(sys.argv[8]) if len(sys.argv) > 8 else None
        test_count_file = Path(sys.argv[9]) if len(sys.argv) > 9 else None
        metrics_dir = None
    else:
        binary_size_file = None
        complexity_file = None
        test_count_file = None

    try:
        # Load required metrics
        test_data = json.loads(test_file.read_text())
        coverage_data = json.loads(coverage_file.read_text())
        isr_data = json.loads(isr_file.read_text())
        quality_data = json.loads(quality_file.read_text())
        ci_data = json.loads(ci_file.read_text())

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

        # Load optional metrics from metrics_dir or individual files
        if metrics_dir and metrics_dir.is_dir():
            optional_metrics = {
                "binary_size": "binary_size.json",
                "complexity": "complexity.json",
                "test_count": "test_count.json",
                "static_analysis": "cppcheck.json",
                "duplicates": "duplicates.json",
            }
            for key, filename in optional_metrics.items():
                data = load_json_safe(metrics_dir / filename)
                if data:
                    aggregated[key] = data
        else:
            # Legacy: individual file arguments
            if binary_size_file and binary_size_file.exists():
                aggregated["binary_size"] = json.loads(binary_size_file.read_text())
            if complexity_file and complexity_file.exists():
                aggregated["complexity"] = json.loads(complexity_file.read_text())
            if test_count_file and test_count_file.exists():
                aggregated["test_count"] = json.loads(test_count_file.read_text())

        # Write output
        output_file.write_text(json.dumps(aggregated, indent=2))
        print(f"✓ Aggregated metrics written to {output_file}")

    except Exception as e:
        print(f"Error aggregating metrics: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
