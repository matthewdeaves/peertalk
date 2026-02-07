#!/usr/bin/env python3
"""Generate HTML report for ISR safety violations"""

import json
import sys
from pathlib import Path
from datetime import datetime, timezone

HTML_TEMPLATE = """<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ISR Safety Report - PeerTalk</title>
    <style>
        :root {{
            --color-success: #28a745;
            --color-error: #dc3545;
            --bg-card: #f8f9fa;
            --text-primary: #212529;
            --text-secondary: #6c757d;
        }}
        * {{ box-sizing: border-box; margin: 0; padding: 0; }}
        body {{
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            line-height: 1.6;
            color: var(--text-primary);
            background: #fff;
            padding: 2rem;
        }}
        .container {{ max-width: 1200px; margin: 0 auto; }}
        h1 {{ margin-bottom: 0.5rem; }}
        .subtitle {{ color: var(--text-secondary); margin-bottom: 2rem; }}
        .summary {{
            background: var(--bg-card);
            padding: 1.5rem;
            border-radius: 8px;
            margin-bottom: 2rem;
            border-left: 4px solid {summary_color};
        }}
        .summary h2 {{ margin-bottom: 1rem; }}
        .summary-stats {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 1rem; }}
        .stat {{ text-align: center; padding: 1rem; background: white; border-radius: 4px; }}
        .stat-value {{ font-size: 2rem; font-weight: bold; color: {summary_color}; }}
        .stat-label {{ color: var(--text-secondary); font-size: 0.9rem; margin-top: 0.5rem; }}
        .violations {{ margin-top: 2rem; }}
        table {{
            width: 100%;
            border-collapse: collapse;
            background: white;
            border-radius: 8px;
            overflow: hidden;
        }}
        thead {{
            background: var(--bg-card);
        }}
        th {{
            padding: 1rem;
            text-align: left;
            font-weight: 600;
            color: var(--text-primary);
            border-bottom: 2px solid #dee2e6;
        }}
        td {{
            padding: 1rem;
            border-bottom: 1px solid #dee2e6;
            vertical-align: top;
        }}
        tbody tr:hover {{
            background: var(--bg-card);
        }}
        .category-badge {{
            display: inline-block;
            background: var(--color-error);
            color: white;
            padding: 0.25rem 0.75rem;
            border-radius: 4px;
            font-size: 0.85rem;
            text-transform: uppercase;
        }}
        .code-context {{
            font-family: 'Courier New', monospace;
            background: var(--bg-card);
            padding: 0.5rem;
            border-radius: 4px;
            margin-top: 0.25rem;
            font-size: 0.9rem;
            overflow-x: auto;
        }}
        .file-link {{
            color: #007bff;
            text-decoration: none;
            font-weight: 500;
        }}
        .file-link:hover {{
            text-decoration: underline;
        }}
        .success-message {{
            text-align: center;
            padding: 3rem;
            color: var(--color-success);
            font-size: 1.5rem;
        }}
        .success-message svg {{
            width: 64px;
            height: 64px;
            margin-bottom: 1rem;
        }}
        a {{ color: #007bff; text-decoration: none; }}
        a:hover {{ text-decoration: underline; }}
    </style>
</head>
<body>
    <div class="container">
        <h1>ISR Safety Report</h1>
        <p class="subtitle">Generated {timestamp} | <a href="/peertalk/">← Back to Dashboard</a></p>

        <div class="summary">
            <h2>Summary</h2>
            <div class="summary-stats">
                <div class="stat">
                    <div class="stat-value">{total_violations}</div>
                    <div class="stat-label">Total Violations</div>
                </div>
                {category_stats}
            </div>
        </div>

        {content}
    </div>
</body>
</html>
"""

def main():
    if len(sys.argv) != 3:
        print("Usage: generate_isr_report.py <isr_json> <output_html>")
        sys.exit(1)

    isr_file = sys.argv[1]
    output_file = sys.argv[2]

    # Load ISR data
    data = json.loads(Path(isr_file).read_text())

    total = data['total_violations']
    by_category = data['by_category']
    violations = data.get('violations', [])

    # Determine summary color
    summary_color = "var(--color-success)" if total == 0 else "var(--color-error)"

    # Generate content
    if total == 0:
        content = '''
        <div class="success-message">
            <svg fill="currentColor" viewBox="0 0 20 20">
                <path fill-rule="evenodd" d="M10 18a8 8 0 100-16 8 8 0 000 16zm3.707-9.293a1 1 0 00-1.414-1.414L9 10.586 7.707 9.293a1 1 0 00-1.414 1.414l2 2a1 1 0 001.414 0l4-4z" clip-rule="evenodd"/>
            </svg>
            <div>No ISR Safety Violations Detected</div>
            <p style="font-size: 1rem; margin-top: 1rem; color: var(--text-secondary);">
                All interrupt-time code follows safe practices.
            </p>
        </div>
        '''
    else:
        violations_html = []
        for v in violations:
            file = v.get('file', 'unknown')
            line = v.get('line', '?')
            callback_name = v.get('callback_name', 'unknown')
            forbidden_call = v.get('forbidden_call', 'unknown')
            reason = v.get('reason', 'No reason provided')
            context = v.get('context', '')
            category = v.get('category', 'unknown')

            # Generate GitHub link
            github_url = f"https://github.com/matthewdeaves/peertalk/blob/develop/{file}#L{line}"

            violations_html.append(f'''
            <tr>
                <td>
                    <a href="{github_url}" class="file-link" target="_blank">{file}:{line}</a>
                </td>
                <td>{callback_name}</td>
                <td><code>{forbidden_call}</code></td>
                <td><span class="category-badge">{category}</span></td>
                <td>
                    {reason}
                    {f'<div class="code-context">{context}</div>' if context else ''}
                </td>
            </tr>
            ''')

        content = f'''
        <div class="violations">
            <h2>Violations ({len(violations)})</h2>
            <table>
                <thead>
                    <tr>
                        <th>Location</th>
                        <th>Callback Function</th>
                        <th>Forbidden Call</th>
                        <th>Category</th>
                        <th>Reason</th>
                    </tr>
                </thead>
                <tbody>
                    {"".join(violations_html) if violations_html else '<tr><td colspan="5">No detailed violation data available.</td></tr>'}
                </tbody>
            </table>
        </div>
        '''

    # Generate category stats
    category_stats_html = []
    for category, count in by_category.items():
        # Capitalize category name for display
        display_name = category.replace('_', ' ').title()
        category_stats_html.append(f'''
                <div class="stat">
                    <div class="stat-value">{count}</div>
                    <div class="stat-label">{display_name}</div>
                </div>
        ''')

    # Generate HTML
    html = HTML_TEMPLATE.format(
        timestamp=datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC"),
        total_violations=total,
        category_stats=''.join(category_stats_html),
        summary_color=summary_color,
        content=content
    )

    # Write output
    Path(output_file).write_text(html)
    print(f"✓ ISR safety report written to {output_file}")

if __name__ == "__main__":
    main()
