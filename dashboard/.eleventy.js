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

  // Load all metrics
  eleventyConfig.addGlobalData("allMetrics", () => {
    const fs = require('fs');
    const path = require('path');
    const metricsDir = path.join(__dirname, '_data', 'metrics');

    if (!fs.existsSync(metricsDir)) return [];

    const files = fs.readdirSync(metricsDir)
      .filter(f => f.endsWith('.json') && f !== 'latest.json')
      .sort()
      .reverse();

    return files.slice(0, 90).map(f => {
      const data = JSON.parse(fs.readFileSync(path.join(metricsDir, f)));
      data.date = f.replace('.json', '');
      return data;
    });
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
