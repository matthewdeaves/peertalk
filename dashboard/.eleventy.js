module.exports = function(eleventyConfig) {
  // Copy static assets
  eleventyConfig.addPassthroughCopy("assets");

  // Date formatting filter
  eleventyConfig.addFilter("dateFormat", (date) => {
    return new Date(date).toLocaleDateString('en-US', {
      year: 'numeric',
      month: 'short',
      day: 'numeric'
    });
  });

  // Percentage change filter
  eleventyConfig.addFilter("percentChange", (current, previous) => {
    if (!previous) return "0";
    return (((current - previous) / previous) * 100).toFixed(1);
  });

  // Truncate commit hash filter
  eleventyConfig.addFilter("truncate", (str, len) => {
    return str ? str.substring(0, len) : "";
  });

  // Load metrics organized by branch
  eleventyConfig.addGlobalData("metricsByBranch", () => {
    const fs = require('fs');
    const path = require('path');
    const metricsDir = path.join(__dirname, '_data', 'metrics');

    if (!fs.existsSync(metricsDir)) return {};

    const result = {};
    const entries = fs.readdirSync(metricsDir, { withFileTypes: true });

    for (const entry of entries) {
      if (entry.isDirectory()) {
        // New structure: _data/metrics/{branch}/{date}.json
        const branchDir = path.join(metricsDir, entry.name);
        const files = fs.readdirSync(branchDir)
          .filter(f => f.endsWith('.json') && f !== 'latest.json')
          .sort()
          .reverse();

        result[entry.name] = files.slice(0, 90).map(f => {
          const data = JSON.parse(fs.readFileSync(path.join(branchDir, f)));
          data.date = f.replace('.json', '');
          return data;
        });
      } else if (entry.isFile() && entry.name.endsWith('.json') && entry.name !== 'latest.json') {
        // Legacy structure: _data/metrics/{date}.json (migrate to 'develop')
        if (!result['develop']) {
          result['develop'] = [];
        }
        const data = JSON.parse(fs.readFileSync(path.join(metricsDir, entry.name)));
        data.date = entry.name.replace('.json', '');
        result['develop'].push(data);
      }
    }

    // Sort legacy metrics
    if (result['develop']) {
      result['develop'].sort((a, b) => b.date.localeCompare(a.date));
      result['develop'] = result['develop'].slice(0, 90);
    }

    return result;
  });

  // Get list of available branches
  eleventyConfig.addGlobalData("availableBranches", () => {
    const fs = require('fs');
    const path = require('path');
    const metricsDir = path.join(__dirname, '_data', 'metrics');

    if (!fs.existsSync(metricsDir)) return ['develop'];

    const branches = new Set(['develop']); // Always include develop
    const entries = fs.readdirSync(metricsDir, { withFileTypes: true });

    for (const entry of entries) {
      if (entry.isDirectory()) {
        branches.add(entry.name);
      }
    }

    // Sort with main first, then develop, then others alphabetically
    return Array.from(branches).sort((a, b) => {
      if (a === 'main') return -1;
      if (b === 'main') return 1;
      if (a === 'develop') return -1;
      if (b === 'develop') return 1;
      return a.localeCompare(b);
    });
  });

  // Default branch for initial display (prefer main if available, otherwise develop)
  eleventyConfig.addGlobalData("defaultBranch", () => {
    const fs = require('fs');
    const path = require('path');
    const metricsDir = path.join(__dirname, '_data', 'metrics');

    if (fs.existsSync(path.join(metricsDir, 'main'))) {
      return 'main';
    }
    return 'develop';
  });

  return {
    dir: {
      input: ".",
      output: "_site",
      includes: "_includes",
      data: "_data"
    },
    templateFormats: ["njk", "md", "html"],
    htmlTemplateEngine: "njk",
    markdownTemplateEngine: "njk"
  };
};
