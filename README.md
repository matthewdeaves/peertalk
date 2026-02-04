# PeerTalk Metrics Dashboard

A comprehensive software metrics dashboard for tracking test results, code coverage, ISR safety, code quality, and CI performance over time.

## Overview

The dashboard is built with:
- **Static Site Generator:** 11ty (Eleventy)
- **Charts:** Chart.js
- **Data Storage:** Daily JSON files on `gh-pages` branch (90-day retention)
- **Deployment:** GitHub Pages via GitHub Actions

## Quick Setup

### Option 1: Automatic Setup (Recommended)

From the project root on the `develop` or `main` branch:

```bash
./tools/setup-dashboard.sh
```

This script will:
1. Create orphan `gh-pages` branch
2. Copy dashboard files
3. Download Chart.js
4. Install dependencies
5. Test the build
6. Commit initial setup

Then push the branch:
```bash
git push -u origin gh-pages
```

### Option 2: Manual Setup

1. Create orphan branch:
```bash
git checkout --orphan gh-pages
git rm -rf .
git clean -fdx
```

2. Copy dashboard files from `dashboard/` directory:
```bash
# From another terminal or clone
cp -r /path/to/develop/dashboard/* .
```

3. Download Chart.js:
```bash
mkdir -p assets/js
curl -L https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js \
  -o assets/js/chart.min.js
```

4. Install dependencies:
```bash
npm install
```

5. Test locally:
```bash
npm run serve
# Open http://localhost:8080
```

6. Commit and push:
```bash
git add .
git commit -m "Initial metrics dashboard setup"
git push -u origin gh-pages
```

### Configure GitHub Pages

1. Go to repository **Settings → Pages**
2. Set **Source** to "GitHub Actions"
3. Wait for deployment workflow to complete
4. Dashboard will be available at: `https://matthewdeaves.github.io/peertalk/`

## How It Works

### Data Flow

```
CI Run on develop/main
  ↓
Extract Metrics (scripts in tools/metrics/)
  ↓
Aggregate to JSON (tools/metrics/aggregate_metrics.py)
  ↓
Commit to gh-pages (_data/metrics/YYYY-MM-DD.json)
  ↓
11ty Build Triggered
  ↓
Deploy to GitHub Pages
```

### Metrics Collected

**Test Results:**
- Total tests
- Passed/failed count
- Pass rate percentage
- Individual test details

**Code Coverage:**
- Line coverage percentage
- Function coverage percentage
- Total lines/functions covered

**ISR Safety:**
- Total violations
- Violations by category (memory, timing, I/O, etc.)

**Code Quality:**
- Files over 500 lines
- TODO/FIXME count
- Compiler warnings

**CI Performance:**
- Build duration
- Test duration
- Coverage generation time
- Total workflow time

## Dashboard Features

### Summary Cards

Four key metrics displayed prominently:
- Test pass rate (green when 100%)
- Line coverage (green when ≥40%)
- ISR violations (green when 0)
- CI build time

### Trend Charts

**Test Pass Rate Trend:**
- Line chart showing test pass rate over last 30 days

**Coverage Trends:**
- Line coverage and function coverage on same chart

**Code Quality:**
- Bar chart showing files >500 lines and TODO count

**CI Performance:**
- Total workflow duration trend

### Historical Data

- Last 90 days of metrics retained
- Automatic cleanup of old data
- Latest metrics always available at `_data/metrics/latest.json`

## Local Development

### Test Dashboard Locally

```bash
git checkout gh-pages
npm install  # First time only
npm run serve
```

Open http://localhost:8080

### Add Sample Data

Create `_data/metrics/2026-02-04.json`:
```json
{
  "timestamp": "2026-02-04T12:00:00Z",
  "commit": "abc1234",
  "branch": "develop",
  "test_results": {...},
  "coverage": {...},
  "isr_safety": {...},
  "quality": {...},
  "ci_metrics": {...}
}
```

See existing files for full schema.

## CI Integration

### Metrics Collection Job

The `collect-metrics` job in `.github/workflows/ci.yml`:
1. Runs after all tests complete
2. Extracts metrics using scripts in `tools/metrics/`
3. Aggregates into single JSON file
4. Commits to `gh-pages` branch
5. Triggers dashboard rebuild

### Dashboard Deployment

The `.github/workflows/deploy-dashboard.yml` workflow:
1. Triggered on push to `gh-pages`
2. Installs npm dependencies
3. Builds site with 11ty
4. Deploys to GitHub Pages

## Troubleshooting

### Dashboard not updating

1. Check CI workflow completed successfully
2. Verify `collect-metrics` job ran (only on `develop`/`main` branches)
3. Check `gh-pages` branch has new metrics file
4. Verify `deploy-dashboard.yml` workflow completed

### Charts not rendering

1. Verify Chart.js is downloaded: `assets/js/chart.min.js`
2. Check browser console for JavaScript errors
3. Ensure metrics JSON is valid: `cat _data/metrics/latest.json | jq .`

### Build failures

1. Ensure Node.js 20+ is installed
2. Delete `node_modules` and run `npm install` again
3. Check `npx @11ty/eleventy` runs without errors

## Customization

### Add New Metric

1. Update extraction script in `tools/metrics/`
2. Modify `aggregate_metrics.py` to include new data
3. Update `index.njk` to display new metric
4. Add chart if needed

### Change Retention Period

Edit `tools/metrics/upload_to_pages.sh`:
```bash
# Change from 90 to desired days
find _data/metrics -name "*.json" -type f -mtime +90 -not -name "latest.json" -delete
```

### Customize Styling

Edit `assets/css/dashboard.css`:
- Colors in `:root` section
- Layout in `.metrics-grid` and `.charts-section`
- Responsive breakpoints in `@media` queries

## Architecture

### File Structure (gh-pages branch)

```
├── .eleventy.js           # 11ty configuration
├── package.json           # npm dependencies
├── index.njk              # Main dashboard page
├── _includes/
│   └── layouts/
│       └── base.njk       # Base HTML template
├── assets/
│   ├── css/
│   │   └── dashboard.css  # Styles
│   └── js/
│       └── chart.min.js   # Chart.js library
├── _data/
│   └── metrics/
│       ├── 2026-02-04.json
│       ├── 2026-02-05.json
│       └── latest.json    # Symlink to latest
└── _site/                 # Generated by 11ty (not committed)
```

### Eleventy Configuration

- **Input:** Current directory
- **Output:** `_site/`
- **Templates:** Nunjucks (`.njk`)
- **Base path:** `/peertalk/` (for GitHub Pages)

### Data Loading

The `allMetrics` global data:
1. Reads all JSON files from `_data/metrics/`
2. Sorts by date (newest first)
3. Limits to 90 most recent
4. Adds `date` field from filename
5. Available in all templates

## Maintenance

### Regular Tasks

**None!** The dashboard is fully automated:
- Metrics collected on every CI run (develop/main)
- Old metrics cleaned up automatically (>90 days)
- Dashboard rebuilds automatically on data changes

### Monitoring

Check dashboard health:
1. Verify latest commit date is recent
2. Check test pass rate is 100%
3. Monitor coverage trend (should be stable or increasing)
4. Watch for ISR violations

## Resources

- [11ty Documentation](https://www.11ty.dev/docs/)
- [Chart.js Documentation](https://www.chartjs.org/docs/)
- [GitHub Pages Documentation](https://docs.github.com/en/pages)
- [Nunjucks Template Language](https://mozilla.github.io/nunjucks/)

## License

Part of the PeerTalk project. See main repository for license information.
