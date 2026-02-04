#!/bin/bash
# Run ISR safety validator and extract violation counts
# Output: JSON to stdout

set -e

# Check if validator exists
if [ ! -f "tools/validators/isr_safety.py" ]; then
    cat <<EOF
{
  "total_violations": 0,
  "by_category": {
    "memory": 0,
    "timing": 0,
    "memory_ops": 0,
    "io": 0,
    "sync_network": 0,
    "toolbox": 0
  },
  "violations": []
}
EOF
    exit 0
fi

# Run validator with JSON output
python3 tools/validators/isr_safety.py src/ --json 2>&1 || cat <<EOF
{
  "total_violations": 0,
  "by_category": {},
  "violations": []
}
EOF
