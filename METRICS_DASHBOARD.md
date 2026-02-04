# Metrics Dashboard Setup Guide

## Quick Start

### 1. Initialize Dashboard (One-Time Setup)

From the `develop` branch:

```bash
./tools/setup-dashboard.sh
```

This creates the `gh-pages` branch with all dashboard files.

### 2. Push to GitHub

```bash
git push -u origin gh-pages
```

### 3. Configure GitHub Pages

1. Go to **Settings → Pages**
2. Set **Source** to "GitHub Actions"
3. Save

### 4. Wait for First Metrics

The dashboard will populate after the next CI run on `develop` or `main`.

Visit: `https://matthewdeaves.github.io/peertalk/`

## What's Included

### Metrics Extraction Scripts (`tools/metrics/`)

- `extract_test_results.sh` - Parse test output
- `extract_coverage.sh` - Parse lcov coverage data
- `extract_isr_violations.sh` - Count ISR safety violations
- `extract_quality_metrics.sh` - File sizes, TODO counts
- `extract_ci_metrics.sh` - CI timing data
- `aggregate_metrics.py` - Combine all metrics into single JSON
- `upload_to_pages.sh` - Commit metrics to gh-pages branch

### CI Workflow Updates (`.github/workflows/`)

- **ci.yml** - Added `collect-metrics` job that runs after tests
- **deploy-dashboard.yml** - Deploys dashboard when gh-pages updates

### Dashboard Files (`dashboard/`)

- **.eleventy.js** - 11ty configuration
- **package.json** - npm dependencies
- **index.njk** - Main dashboard page with charts
- **_includes/layouts/base.njk** - HTML template
- **assets/css/dashboard.css** - Styling
- **_data/metrics/** - Sample metrics data

## How It Works

```
1. Push to develop/main
   ↓
2. CI runs tests and builds
   ↓
3. collect-metrics job extracts data
   ↓
4. Metrics committed to gh-pages branch
   ↓
5. deploy-dashboard workflow triggers
   ↓
6. 11ty builds static site
   ↓
7. Deployed to GitHub Pages
```

## Dashboard Features

**Summary Cards:**
- Test pass rate
- Code coverage
- ISR safety violations
- CI build time

**Trend Charts (30 days):**
- Test pass rate over time
- Coverage trends (line + function)
- Code quality metrics
- CI performance

**Data Retention:**
- 90 days of historical metrics
- Automatic cleanup of old data

## Local Testing

### Test Extraction Scripts

```bash
# Generate test output
make test > /tmp/test_output.txt

# Test extraction
./tools/metrics/extract_test_results.sh /tmp/test_output.txt | python3 -m json.tool

# Test all metrics
make coverage
./tools/metrics/extract_coverage.sh | python3 -m json.tool
./tools/metrics/extract_quality_metrics.sh | python3 -m json.tool
./tools/metrics/extract_isr_violations.sh | python3 -m json.tool
./tools/metrics/extract_ci_metrics.sh | python3 -m json.tool
```

### Test Dashboard Locally

```bash
git checkout gh-pages
npm install
npm run serve
# Open http://localhost:8080
```

## Customization

### Add New Metric

1. Update relevant extraction script in `tools/metrics/`
2. Modify `aggregate_metrics.py` to include new field
3. Update `dashboard/index.njk` to display it
4. Add chart if visualization needed

### Change Colors/Styling

Edit `dashboard/assets/css/dashboard.css`:
- Colors: `:root` section
- Layout: `.metrics-grid`, `.charts-section`

### Adjust Retention Period

Edit `tools/metrics/upload_to_pages.sh`:
```bash
# Change 90 to desired days
find _data/metrics -name "*.json" -type f -mtime +90 -delete
```

## Troubleshooting

**Dashboard not updating:**
- Check `collect-metrics` job completed in CI
- Verify metrics committed to gh-pages: `git log origin/gh-pages`
- Check GitHub Actions tab for deployment status

**Charts not showing:**
- Verify Chart.js downloaded: `dashboard/assets/js/chart.min.js`
- Check browser console for errors
- Validate metrics JSON: `cat _data/metrics/latest.json | jq .`

**Build failures:**
- Ensure Node.js 20+ installed
- Delete `node_modules`, run `npm install`
- Test build: `npx @11ty/eleventy`

## Files Changed

```
tools/metrics/
  ├── extract_test_results.sh       (new)
  ├── extract_coverage.sh            (new)
  ├── extract_isr_violations.sh      (new)
  ├── extract_quality_metrics.sh     (new)
  ├── extract_ci_metrics.sh          (new)
  ├── aggregate_metrics.py           (new)
  └── upload_to_pages.sh             (new)

tools/
  └── setup-dashboard.sh             (new)

.github/workflows/
  ├── ci.yml                         (modified - added collect-metrics job)
  └── deploy-dashboard.yml           (new)

dashboard/                           (new - for gh-pages branch)
  ├── .eleventy.js
  ├── package.json
  ├── .gitignore
  ├── index.njk
  ├── README.md
  ├── _includes/layouts/base.njk
  ├── assets/css/dashboard.css
  └── _data/metrics/2026-02-04.json  (sample)
```

## Next Steps

1. Run `./tools/setup-dashboard.sh` to create gh-pages branch
2. Push gh-pages branch: `git push -u origin gh-pages`
3. Configure GitHub Pages in repo settings
4. Wait for first CI run to populate metrics
5. Visit dashboard at GitHub Pages URL

## Resources

- [Dashboard README](dashboard/README.md) - Detailed documentation
- [11ty Docs](https://www.11ty.dev/docs/)
- [Chart.js Docs](https://www.chartjs.org/docs/)
- [GitHub Pages Docs](https://docs.github.com/en/pages)
