# Classic Mac Hardware

All Classic Mac file operations MUST use the classic-mac-hardware MCP server tools.
Never write raw FTP scripts, curl commands, or direct socket connections.

The MCP server handles rate limiting, path normalization, and retry logic
required for RumpusFTP compatibility.

Run `list_machines` to see available machines.
Run one LaunchAPPL test at a time — they bind to shared network ports.

See https://github.com/matthewdeaves/classic-mac-hardware-mcp for full documentation.
