# /setup-machine Implementation Guide

This document describes the implementation of the /setup-machine skill for Claude to follow.

## Critical Rules

1. **ALWAYS use MCP tools** - Never use raw Python/Bash for FTP operations. All FTP must go through `mcp__classic-mac-hardware__*` tools.
2. **Empty FTP root is normal** - Don't probe or explore the FTP server. Just create the required directories.
3. **Check prerequisites first** - Verify MCP server and LaunchAPPL source exist before starting.
4. **NO CLEVER PARSING** - Take user input EXACTLY as provided. If unclear, ask explicit follow-up questions.
   - DO NOT try to infer IP ranges - ask for complete IP upfront
   - DO NOT try to parse complex credential answers - ask username and password separately
   - DO NOT assume or guess - be direct and explicit

## Workflow

When the user runs `/setup-machine`, follow these steps:

### 0. Verify Prerequisites (MUST DO FIRST)

Before collecting any information, verify the MCP server is available:

```
# Try to call list_machines - if it fails, MCP isn't configured
mcp__classic-mac-hardware__list_machines
```

**If MCP call fails:**
```
The classic-mac-hardware MCP server isn't configured yet.

Please run these commands first:
1. cp .mcp.json.example .mcp.json
2. Restart Claude Code (or run /mcp to reload)
3. Then run /setup-machine again
```

Also check if LaunchAPPL source exists:
```bash
ls LaunchAPPL/Server/  # Check if source code exists
```

**If LaunchAPPL/ doesn't exist:**
- Note this but don't fail - continue with setup
- Skip the build/deploy steps
- Inform user that LaunchAPPL will be available after implementation phases begin

### 1. Collect Information

Use **AskUserQuestion** to collect the following information interactively.

**CRITICAL RULES FOR DATA COLLECTION:**
- **NO clever parsing** - Take user input exactly as given
- **NO assumptions** - If unclear, ask again explicitly
- **NO multi-step refinement** - Ask for complete values upfront
- **Each question should be CLEAR and DIRECT**

```
Question 1: Machine ID
  Header: "Machine ID"
  Prompt: "What's the machine nickname (e.g., 'performa6400', 'se30', 'iici')?"
  - User types a value (text input)
  - TAKE EXACTLY AS PROVIDED - no validation yet

Question 2: IP Address
  Header: "IP Address"
  Prompt: "What's the complete IP address? (e.g., '10.188.1.102')"
  - User types the FULL IP address
  - DO NOT ask for ranges or partial IPs
  - DO NOT try to infer or complete partial IPs
  - If user provides incomplete IP (like "10.188.1.x"), ask again:
    "I need the complete IP address. What is the full IP?"

Question 3: Platform
  Header: "Platform"
  Prompt: "Which networking platform does this Mac use?"
  Options:
    - "MacTCP (System 6.0.8 - 7.5.5, 68k Macs)"
    - "Open Transport (System 7.6.1+, PPC/68040 Macs)"

Question 4: FTP Username
  Header: "FTP Username"
  Prompt: "What's the FTP username for this Mac?"
  - User types username (text input)
  - TAKE EXACTLY AS PROVIDED
  - DO NOT try to parse phrases like "mac for both"

Question 5: FTP Password
  Header: "FTP Password"
  Prompt: "What's the FTP password for this Mac?"
  - User types password (text input)
  - TAKE EXACTLY AS PROVIDED
  - DO NOT try to parse complex answers
  - If user says something like "same as username", ask explicitly:
    "What's the password value? (Type the password)"

Question 6: System Version
  Header: "System Version"
  Prompt: "What's the System version? (e.g., '7.5.3', '7.6.1', '8.1')"
  - User types version (text input)

Question 7: Machine Description
  Header: "Description"
  Prompt: "Choose a description for this machine:"
  Options:
    - Generate 2-3 suggested descriptions based on collected info:
      * If platform=mactcp: "[Machine Name] - 68k Mac running System [version]"
      * If platform=opentransport: "[Machine Name] - PPC Mac running System [version]"
      * Example: "Performa 6400 - PPC Mac running System 7.6.1"
      * Example: "SE/30 - 68k Mac running System 7.5.3"
    - "Custom description" (if selected, prompt: "Enter custom description:")
    - "Leave blank" (sets notes to empty string)

  **IMPORTANT:** If user selects "Custom description":
    - MUST immediately ask: "What description would you like?"
    - MUST use their exact answer as the description
    - DO NOT use placeholder text like "TODO: Add description"
    - DO NOT tell them to "update later"
```

**IMPORTANT:** If any answer is ambiguous or incomplete:
1. DO NOT try to parse or infer the intent
2. Ask a follow-up question that is COMPLETELY EXPLICIT
3. Example: "I need the complete IP address. Please type the full IP (like 10.188.1.102):"

**Multi-step Questions:**
- ONLY use multi-step questions when offering suggested values with a "custom" option
- Example: Description question offers suggested descriptions + "Custom" option
- If "Custom" is selected, IMMEDIATELY ask for the custom value
- DO NOT use placeholder text or tell users to "update later"
- TAKE their custom input EXACTLY as provided

### 2. Validate Input

- **Machine ID:** Check that it doesn't already exist in machines.json
  - Must be lowercase, alphanumeric, hyphens/underscores allowed
  - Example: "performa6400", "se_30", "iici-1"

- **IP Address:** Must be valid IPv4 format
  - Check regex: `^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}$`
  - If incomplete (contains 'x', missing octets), ask explicitly:
    "I need a complete IP address. Please provide the full IP (e.g., 10.188.1.102):"

- **Platform:** Must be one of: mactcp, opentransport
  - Map user-friendly names: "MacTCP" → "mactcp", "Open Transport" → "opentransport"

- **FTP Credentials:**
  - Username and password must be non-empty strings
  - DO NOT try to parse complex answers - take exactly as typed
  - If user provides confusing answer (like "mac for both"), ask TWO separate questions:
    1. "What is the FTP username?"
    2. "What is the FTP password?"

If validation fails, provide clear error messages with examples and re-prompt.

### 3. Read Existing machines.json

```bash
Read: .claude/mcp-servers/classic-mac-hardware/machines.json
```

If file doesn't exist, start with empty `{}`.

### 4. Add New Machine Entry

**CORRECT JSON FORMAT** (nested structure with ftp config):

```json
{
  "performa6400": {
    "name": "Performa 6400",
    "platform": "opentransport",
    "system": "System 7.6.1",
    "cpu": "PPC 603e",
    "ftp": {
      "host": "10.188.1.102",
      "port": 21,
      "username": "mac",
      "password": "mac",
      "paths": {
        "binaries": "Applications:PeerTalk",
        "logs": "Documents:PeerTalk-Logs",
        "temp": "Temp",
        "launchappl": "Applications:LaunchAPPLServer"
      }
    },
    "notes": "Performa 6400/180 - PPC"
  }
}
```

**Platform mapping:**
- "MacTCP" → "mactcp"
- "Open Transport" → "opentransport"

**Path format:** Use Mac-style colons for subdirectories (e.g., `Applications:PeerTalk` not `/Applications/PeerTalk/`). This is what RumpusFTP expects.

Write the updated JSON back to machines.json.

### 5. Reload MCP Configuration

```bash
mcp__classic-mac-hardware__reload_config
```

This tells the MCP server to reload machines.json and pick up the new machine.

### 6. Test FTP Connectivity

```bash
mcp__classic-mac-hardware__test_connection machine=<machine_id> test_launchappl=false
```

This verifies:
- FTP server is reachable
- Credentials are correct

If this fails, provide troubleshooting steps:
- Check if Mac is powered on
- Check if RumpusFTP is running
- Verify IP address
- Verify username/password

### 6.5. Create Directory Structure on Mac

**IMPORTANT:** Empty FTP root is NORMAL. Don't probe or explore - just create the directories.

Use MCP `create_directory` tool to create required folders:

```bash
mcp__classic-mac-hardware__create_directory machine=<machine_id> path="Applications"
mcp__classic-mac-hardware__create_directory machine=<machine_id> path="Applications:PeerTalk"
mcp__classic-mac-hardware__create_directory machine=<machine_id> path="Applications:LaunchAPPLServer"
mcp__classic-mac-hardware__create_directory machine=<machine_id> path="Documents"
mcp__classic-mac-hardware__create_directory machine=<machine_id> path="Documents:PeerTalk-Logs"
mcp__classic-mac-hardware__create_directory machine=<machine_id> path="Temp"
```

If directories already exist, the tool will handle it gracefully. Don't fail on "already exists" errors.

### 7. Build LaunchAPPLServer

**First, check if LaunchAPPL source exists:**

```bash
ls LaunchAPPL/Server/LaunchAPPLServer.c
```

**If LaunchAPPL/ doesn't exist:**
- Skip build and deploy steps
- Note in output: "LaunchAPPLServer source not yet implemented"
- Tell user: "LaunchAPPL will be available after project implementation begins. For now, FTP-only workflow is available."
- Continue to step 9 (provide instructions)

**If source exists, determine build based on platform:**

```bash
Platform: mactcp       → ./scripts/build-launcher.sh mactcp
Platform: opentransport → ./scripts/build-launcher.sh ot
```

The build script will:
- Run in Docker container
- Use Retro68 toolchain
- Output both .bin (MacBinary) and .dsk (disk image)

**Expected outputs:**
- MacTCP: `LaunchAPPL/build-mactcp/Server/LaunchAPPLServer-MacTCP.bin` and `.dsk`
- Open Transport: `LaunchAPPL/build-ppc/Server/LaunchAPPLServer-OpenTransport.bin` and `.dsk`

### 8. Deploy LaunchAPPLServer

**Skip this step if LaunchAPPL source doesn't exist (see step 7).**

Map platform to binary paths:

```
Platform: mactcp
  bin_path: LaunchAPPL/build-mactcp/Server/LaunchAPPLServer-MacTCP.bin
  dsk_path: LaunchAPPL/build-mactcp/Server/LaunchAPPLServer-MacTCP.dsk

Platform: opentransport
  bin_path: LaunchAPPL/build-ppc/Server/LaunchAPPLServer-OpenTransport.bin
  dsk_path: LaunchAPPL/build-ppc/Server/LaunchAPPLServer-OpenTransport.dsk
```

Deploy BOTH files using MCP tool:

```bash
# Deploy .bin (MacBinary format)
mcp__classic-mac-hardware__deploy_binary machine=<machine_id> binary_path=<bin_path>

# Deploy .dsk (disk image with resource fork)
mcp__classic-mac-hardware__deploy_binary machine=<machine_id> binary_path=<dsk_path>
```

The MCP tool uploads to the `launchappl` path configured in machines.json (e.g., `Applications:LaunchAPPLServer`).

### 9. Provide User Instructions

Output clear next steps for the user:

```
✓ Machine added: <machine_id>
✓ FTP connectivity verified
✓ Directory structure created
✓ LaunchAPPLServer built for <platform>
✓ Deployed .bin and .dsk files

Next steps on your <machine_name>:
1. Navigate to Applications:LaunchAPPLServer folder
2. Use BinUnpk to extract LaunchAPPLServer from the .bin file
   (or mount the .dsk disk image)
3. Launch the LaunchAPPLServer application
4. Enable "TCP Server" on port 1984 in preferences
5. Test connectivity: /test-machine <machine_id>

Once LaunchAPPL is running, you can:
  /deploy <machine_id> <platform>   # Deploy PeerTalk builds
  /fetch-logs <machine_id>          # Retrieve PT_Log output
  /implement <session>              # Start development work
```

## Error Handling

### Unclear User Input
**NEVER try to parse or infer unclear answers. Instead:**
- If IP is incomplete (like "10.188.1.x"): Ask "What is the complete IP address? (e.g., 10.188.1.102)"
- If credentials are ambiguous (like "mac for both"): Ask TWO separate questions:
  1. "What is the FTP username?"
  2. "What is the FTP password?"
- Take each answer EXACTLY as typed - no clever parsing

### FTP Connection Failed
- Provide clear error message
- Check: IP, FTP server running, credentials
- Suggest manual FTP test

### Build Failed
- Check Docker is running
- Check Retro68 toolchain is built
- Show build logs

### Deploy Failed
- Check disk space on Mac
- Check FTP permissions
- Suggest creating /Applications/LaunchAPPLServer/ manually

## Example Implementation

```typescript
// Pseudo-code for skill implementation

async function setupMachine() {
  // 0. Check MCP is available
  try {
    await mcpTool("classic-mac-hardware", "list_machines");
  } catch {
    console.log("MCP not configured. Run: cp .mcp.json.example .mcp.json");
    return;
  }

  // 1. Collect info
  const answers = await askUserQuestions([
    { question: "Machine ID?", ... },
    { question: "IP address?", ... },
    { question: "Platform?", options: ["MacTCP", "Open Transport"] },
    // ... etc
  ]);

  // 2. Validate
  const machinesJson = readFile(".claude/mcp-servers/classic-mac-hardware/machines.json") || "{}";
  const machines = JSON.parse(machinesJson);

  if (machines[answers.machineId]) {
    throw new Error(`Machine ${answers.machineId} already exists`);
  }

  // 3. Add to machines.json (CORRECT nested format)
  machines[answers.machineId] = {
    name: answers.machineName,
    platform: platformMap[answers.platform],  // "mactcp" or "opentransport"
    system: answers.systemVersion,
    cpu: answers.cpu,
    ftp: {
      host: answers.ipAddress,
      port: 21,
      username: answers.ftpUsername,
      password: answers.ftpPassword,
      paths: {
        binaries: "Applications:PeerTalk",
        logs: "Documents:PeerTalk-Logs",
        temp: "Temp",
        launchappl: "Applications:LaunchAPPLServer"
      }
    },
    notes: answers.description
  };

  writeFile(".claude/mcp-servers/classic-mac-hardware/machines.json", JSON.stringify(machines, null, 2));

  // 4. Reload config
  await mcpTool("classic-mac-hardware", "reload_config");

  // 5. Test FTP
  await mcpTool("classic-mac-hardware", "test_connection", {
    machine: answers.machineId,
    test_launchappl: false
  });

  // 6. Create directories (empty FTP root is normal!)
  for (const path of ["Applications", "Applications:PeerTalk", "Applications:LaunchAPPLServer",
                      "Documents", "Documents:PeerTalk-Logs", "Temp"]) {
    await mcpTool("classic-mac-hardware", "create_directory", {
      machine: answers.machineId,
      path: path
    });
  }

  // 7. Check if LaunchAPPL source exists
  if (!fileExists("LaunchAPPL/Server/LaunchAPPLServer.c")) {
    console.log("LaunchAPPL source not yet implemented - skipping build/deploy");
    return;
  }

  // 8. Build LaunchAPPLServer
  const buildPlatform = platformToBuild[answers.platform];
  await bash(`./scripts/build-launcher.sh ${buildPlatform}`);

  // 9. Deploy both .bin and .dsk files
  const binPath = platformToBinaryPath[answers.platform];
  const dskPath = binPath.replace('.bin', '.dsk');
  await mcpTool("classic-mac-hardware", "deploy_binary", { machine: answers.machineId, binary_path: binPath });
  await mcpTool("classic-mac-hardware", "deploy_binary", { machine: answers.machineId, binary_path: dskPath });

  // 10. Provide instructions
  console.log(`
    ✓ Machine added: ${answers.machineId}
    ✓ FTP connectivity verified
    ✓ Directory structure created
    ✓ LaunchAPPLServer built and deployed

    Next steps: Use BinUnpk on Mac to extract the .bin file...
  `);
}
```

## Files Modified

- `.claude/mcp-servers/classic-mac-hardware/machines.json` - Machine added
- `LaunchAPPL/build-mactcp/Server/LaunchAPPLServer-MacTCP.bin` - Built (if mactcp)
- `LaunchAPPL/build-ppc/Server/LaunchAPPLServer-OpenTransport.bin` - Built (if ot)

## Dependencies

- MCP server must be configured (`.mcp.json` exists with classic-mac-hardware)
- Docker must be running
- Retro68 toolchain must be built in Docker container
- machines.json may or may not exist (will be created if missing)
- LaunchAPPL/ directory may not exist yet (gracefully skip build/deploy)

## Common Mistakes to Avoid

1. **Don't use raw Python/Bash for FTP** - Always use MCP tools
2. **Don't probe empty FTP root** - Just create directories
3. **Don't assume LaunchAPPL exists** - Check first, skip gracefully if missing
4. **Don't use wrong JSON format** - Use nested ftp structure with paths
5. **Don't use slashes in Mac paths** - Use colons (e.g., `Applications:PeerTalk`)
