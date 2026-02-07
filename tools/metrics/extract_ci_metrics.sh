#!/bin/bash
# Extract CI build and test timing
# Uses environment variables from GitHub Actions
# Output: JSON to stdout

set -e

# Default values (will be overridden in CI environment)
WORKFLOW_NAME="${GITHUB_WORKFLOW:-PeerTalk CI}"
BUILD_DURATION="${BUILD_DURATION_SEC:-0}"
TEST_DURATION="${TEST_DURATION_SEC:-0}"
COVERAGE_DURATION="${COVERAGE_DURATION_SEC:-0}"

# Calculate total (in CI this would be from job timing)
TOTAL_DURATION=$((BUILD_DURATION + TEST_DURATION + COVERAGE_DURATION))

# Job statuses (would be populated from GitHub Actions context)
BUILD_STATUS="${BUILD_STATUS:-success}"
ISR_STATUS="${ISR_STATUS:-success}"
QUALITY_STATUS="${QUALITY_STATUS:-success}"

cat <<EOF
{
  "workflow_name": "$WORKFLOW_NAME",
  "build_duration_sec": $BUILD_DURATION,
  "test_duration_sec": $TEST_DURATION,
  "coverage_duration_sec": $COVERAGE_DURATION,
  "total_duration_sec": $TOTAL_DURATION,
  "jobs": {
    "build-posix": {"status": "$BUILD_STATUS", "duration_sec": $BUILD_DURATION},
    "validate-isr-safety": {"status": "$ISR_STATUS", "duration_sec": 0},
    "quality-gates": {"status": "$QUALITY_STATUS", "duration_sec": 0}
  }
}
EOF
