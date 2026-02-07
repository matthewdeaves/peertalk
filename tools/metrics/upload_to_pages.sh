#!/bin/bash
# Upload aggregated metrics, coverage HTML, and ISR report to gh-pages branch
set -e

if [ $# -lt 1 ]; then
    echo "Usage: upload_to_pages.sh <metrics_file> [coverage_html_dir] [isr_report_html]"
    exit 1
fi

METRICS_FILE="$(realpath "$1")"
COVERAGE_DIR="${2:-}"
ISR_REPORT="${3:-}"
DATE=$(date +%Y-%m-%d)
BRANCH="${GITHUB_REF_NAME:-develop}"
TEMP_DIR=$(mktemp -d)

echo "Branch: $BRANCH"
echo "Date: $DATE"

# Resolve coverage directory to absolute path before changing directories
if [ -n "$COVERAGE_DIR" ] && [ -d "$COVERAGE_DIR" ]; then
    COVERAGE_DIR="$(realpath "$COVERAGE_DIR")"
    echo "Coverage directory: $COVERAGE_DIR"
fi

# Resolve ISR report to absolute path
if [ -n "$ISR_REPORT" ] && [ -f "$ISR_REPORT" ]; then
    ISR_REPORT="$(realpath "$ISR_REPORT")"
    echo "ISR report: $ISR_REPORT"
fi

echo "Cloning gh-pages branch..."
# Clone with recent history to handle concurrent pushes
git clone --depth 5 --branch gh-pages \
  "https://x-access-token:${GITHUB_TOKEN}@github.com/${GITHUB_REPOSITORY}.git" \
  "$TEMP_DIR"

cd "$TEMP_DIR"

# Pull latest changes to avoid conflicts
git pull --rebase origin gh-pages || true

# Create metrics directory for this branch
mkdir -p "_data/metrics/${BRANCH}"

# Copy metrics with date-based filename into branch subdirectory
cp "$METRICS_FILE" "_data/metrics/${BRANCH}/${DATE}.json"
cp "$METRICS_FILE" "_data/metrics/${BRANCH}/latest.json"

# Cleanup old metrics (keep 90 days) - check all branch directories
find _data/metrics -name "*.json" -type f -mtime +90 -not -name "latest.json" -delete || true

# Copy coverage HTML if provided
if [ -n "$COVERAGE_DIR" ] && [ -d "$COVERAGE_DIR" ]; then
    echo "Copying coverage HTML report..."
    mkdir -p coverage
    cp -r "$COVERAGE_DIR"/* coverage/
    git add coverage/
fi

# Copy ISR report if provided
if [ -n "$ISR_REPORT" ] && [ -f "$ISR_REPORT" ]; then
    echo "Copying ISR safety report..."
    cp "$ISR_REPORT" isr-report.html
    git add isr-report.html
fi

# Commit and push
git config user.name "GitHub Actions"
git config user.email "actions@github.com"
git add _data/metrics/
git commit -m "Update metrics and coverage for ${DATE} [skip ci]" || {
    echo "No changes to commit"
    exit 0
}
git push origin gh-pages

# Trigger the deploy-dashboard workflow to rebuild the static site
# The [skip ci] in commit message prevents auto-trigger, so we need to dispatch manually
echo "Triggering dashboard deploy workflow..."
gh workflow run deploy-dashboard.yml --ref gh-pages || {
    echo "Warning: Could not trigger deploy workflow (gh CLI may not be available)"
}

echo "✓ Metrics uploaded successfully"
