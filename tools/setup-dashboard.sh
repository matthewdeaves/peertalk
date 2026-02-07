#!/bin/bash
# Setup script for initializing the gh-pages branch with the metrics dashboard

set -e

echo "=========================================="
echo "  PeerTalk Metrics Dashboard Setup"
echo "=========================================="

# Check if we're in the project root
if [ ! -f "CLAUDE.md" ]; then
    echo "Error: Must run from project root directory"
    exit 1
fi

# Check if dashboard directory exists
if [ ! -d "dashboard" ]; then
    echo "Error: dashboard directory not found"
    exit 1
fi

# Save dashboard files to temp location BEFORE creating orphan branch
TEMP_DASHBOARD=$(mktemp -d)
echo "Saving dashboard files to temporary location..."
cp -r dashboard/* "$TEMP_DASHBOARD/"
cp dashboard/.eleventy.js "$TEMP_DASHBOARD/"
cp dashboard/.gitignore "$TEMP_DASHBOARD/"
echo "✓ Dashboard files saved to $TEMP_DASHBOARD"

# Check if gh-pages branch already exists
if git show-ref --verify --quiet refs/heads/gh-pages; then
    echo ""
    echo "Warning: gh-pages branch already exists"
    read -p "Do you want to recreate it? This will delete all existing data. (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Aborting."
        rm -rf "$TEMP_DASHBOARD"
        exit 1
    fi
    git branch -D gh-pages
fi

echo ""
echo "Step 1: Creating orphan gh-pages branch..."
git checkout --orphan gh-pages
git rm -rf . 2>/dev/null || true
git clean -fdx 2>/dev/null || true

echo ""
echo "Step 2: Copying dashboard files..."
cp -r "$TEMP_DASHBOARD/"* .
cp "$TEMP_DASHBOARD/.eleventy.js" .
cp "$TEMP_DASHBOARD/.gitignore" .
rm -rf "$TEMP_DASHBOARD"
echo "✓ Dashboard files copied"

# Add .finf and .rsrc to gitignore (Mac resource fork artifacts)
if ! grep -q ".finf/" .gitignore; then
    echo ".finf/" >> .gitignore
    echo ".rsrc/" >> .gitignore
fi

echo ""
echo "Step 3: Downloading Chart.js..."
mkdir -p assets/js
if curl -L https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js \
  -o assets/js/chart.min.js; then
    echo "✓ Chart.js downloaded"
else
    echo "✗ Failed to download Chart.js"
    exit 1
fi

echo ""
echo "Step 4: Installing npm dependencies..."
if npm install --silent; then
    echo "✓ npm dependencies installed"
else
    echo "✗ Failed to install npm dependencies"
    exit 1
fi

echo ""
echo "Step 5: Testing build..."
if npx @11ty/eleventy; then
    echo "✓ Build successful"
else
    echo "✗ Build failed"
    exit 1
fi

echo ""
echo "Step 6: Committing initial dashboard..."
git add .
git commit -m "Initial metrics dashboard setup

- 11ty static site with Chart.js visualizations
- 4 summary cards and 4 trend charts
- Sample metrics data included
- Responsive design ready for GitHub Pages

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"

echo ""
echo "=========================================="
echo "  Setup Complete!"
echo "=========================================="
echo ""
echo "Next steps:"
echo "1. Push gh-pages branch:"
echo "   git push -u origin gh-pages"
echo ""
echo "2. Configure GitHub Pages:"
echo "   - Go to: https://github.com/matthewdeaves/peertalk/settings/pages"
echo "   - Set Source to 'GitHub Actions'"
echo "   - Click Save"
echo ""
echo "3. Trigger metrics collection:"
echo "   git checkout develop"
echo "   git commit --allow-empty -m 'Trigger metrics collection'"
echo "   git push origin develop"
echo ""
echo "4. View your dashboard:"
echo "   https://matthewdeaves.github.io/peertalk/"
echo ""
echo "To test locally:"
echo "  git checkout gh-pages"
echo "  npm run serve"
echo "  Open http://localhost:8080"
echo ""
