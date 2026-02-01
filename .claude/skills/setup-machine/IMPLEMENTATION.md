# /setup-machine Implementation Guide

This document describes the implementation of the /setup-machine skill for Claude to follow.

## Workflow

When the user runs `/setup-machine`, follow these steps:

### 1. Collect Information

Use **AskUserQuestion** to collect the following information interactively:

```
Question 1: Machine ID
  Header: "Machine ID"
  Prompt: "What's the machine nickname (e.g., 'performa6400', 'se30')?"
  Options:
    - Provide 2-3 example IDs
    - User can enter custom value (this is text input, not multiple choice)

Question 2: IP Address
  Prompt: "What's the IP address of this Mac?"

Question 3: Platform
  Header: "Platform"
  Prompt: "Which networking platform does this Mac use?"
  Options:
    - "MacTCP" (System 6.0.8 - 7.5.5, 68k)
    - "Open Transport" (System 7.6.1+, PPC/68040)
    - "AppleTalk" (Any Mac with LocalTalk/EtherTalk)

Question 4: FTP Username
  Prompt: "What's the FTP username?"

Question 5: FTP Password
  Prompt: "What's the FTP password?"
  (Use secure input if available)

Question 6: System Version
  Prompt: "What's the System version (e.g., '7.5.3', '7.6.1')?"

Question 7: Description (optional)
  Prompt: "Optional description (e.g., 'Performa 6400/200 - PPC'):"
```

### 2. Validate Input

- **Machine ID:** Check that it doesn't already exist in machines.json
- **IP Address:** Basic format validation (IPv4)
- **Platform:** Must be one of: mactcp, opentransport, appletalk

If validation fails, provide clear error messages and re-prompt.

### 3. Read Existing machines.json

```bash
Read: .claude/mcp-servers/classic-mac-hardware/machines.json
```

Parse the JSON to check for existing machines.

### 4. Add New Machine Entry

Append the new machine to machines.json:

```json
{
  "machine_id": {
    "id": "machine_id",
    "name": "Display Name",
    "ip": "10.188.1.213",
    "ftp_user": "peertalk",
    "ftp_password": "password",
    "platform": "mactcp",
    "system_version": "7.5.3",
    "description": "Performa 6200/75 - 68040"
  }
}
```

**Platform mapping:**
- "MacTCP" → "mactcp"
- "Open Transport" → "opentransport"
- "AppleTalk" → "appletalk"

Write the updated JSON back to machines.json.

### 5. Reload MCP Configuration

```bash
mcp__classic-mac-hardware__reload_config
```

This tells the MCP server to reload machines.json and pick up the new machine.

### 6. Test FTP Connectivity

```bash
mcp__classic-mac-hardware__test_connection \
  machine=<machine_id> \
  test_launchappl=false
```

This verifies:
- FTP server is reachable
- Credentials are correct
- Can list directories

If this fails, provide troubleshooting steps:
- Check if Mac is powered on
- Check if RumpusFTP is running
- Verify IP address
- Verify username/password

### 7. Build LaunchAPPLServer

Determine which build to run based on platform:

```bash
Platform: mactcp       → ./scripts/build-launcher.sh mactcp
Platform: opentransport → ./scripts/build-launcher.sh ot
Platform: appletalk    → ./scripts/build-launcher.sh mactcp  # Use MacTCP build for now
```

The build script will:
- Run in Docker
- Use Retro68 toolchain
- Output .bin and .dsk files

**Expected outputs:**
- MacTCP: `LaunchAPPL/build-mactcp/Server/LaunchAPPLServer-MacTCP.bin`
- Open Transport: `LaunchAPPL/build-ppc/Server/LaunchAPPLServer-OpenTransport.bin`

### 8. Deploy LaunchAPPLServer

Map platform to binary path:

```bash
Platform: mactcp
  binary_path: LaunchAPPL/build-mactcp/Server/LaunchAPPLServer-MacTCP.bin

Platform: opentransport
  binary_path: LaunchAPPL/build-ppc/Server/LaunchAPPLServer-OpenTransport.bin

Platform: appletalk
  binary_path: LaunchAPPL/build-mactcp/Server/LaunchAPPLServer-MacTCP.bin
```

Deploy using MCP tool:

```bash
mcp__classic-mac-hardware__deploy_binary \
  machine=<machine_id> \
  platform=<platform> \
  binary_path=<binary_path>
```

This uploads the .bin file to `/Applications/LaunchAPPLServer/` on the Mac via FTP.

### 9. Provide User Instructions

Output clear next steps for the user:

```
✓ Machine added: <machine_id>
✓ FTP connectivity verified
✓ LaunchAPPLServer built for <platform>
✓ Deployed to <machine_id>

Next steps:
1. On your <machine_name>, navigate to /Applications/LaunchAPPLServer/
2. Use BinUnpk to extract LaunchAPPLServer from LaunchAPPLServer-<Platform>.bin
3. Launch the LaunchAPPLServer application
4. In the app, enable "TCP Server" on port 1984
5. Run this command to test LaunchAPPL connectivity:
   mcp__classic-mac-hardware__test_connection machine=<machine_id>

Once LaunchAPPL is running, you can:
  /deploy <machine_id> <platform>   # Deploy PeerTalk builds
  /fetch-logs <machine_id>          # Retrieve PT_Log output
  /implement <session>              # Start development work
```

## Error Handling

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
  // 1. Collect info
  const answers = await askUserQuestions([
    { question: "Machine ID?", ... },
    { question: "IP address?", ... },
    { question: "Platform?", options: ["MacTCP", "Open Transport", "AppleTalk"] },
    // ... etc
  ]);

  // 2. Validate
  const machinesJson = readFile(".claude/mcp-servers/classic-mac-hardware/machines.json");
  const machines = JSON.parse(machinesJson);

  if (machines[answers.machineId]) {
    throw new Error(`Machine ${answers.machineId} already exists`);
  }

  // 3. Add to machines.json
  machines[answers.machineId] = {
    id: answers.machineId,
    name: answers.machineName,
    ip: answers.ipAddress,
    ftp_user: answers.ftpUsername,
    ftp_password: answers.ftpPassword,
    platform: platformMap[answers.platform],
    system_version: answers.systemVersion,
    description: answers.description
  };

  writeFile(".claude/mcp-servers/classic-mac-hardware/machines.json", JSON.stringify(machines, null, 2));

  // 4. Reload config
  await mcpTool("classic-mac-hardware", "reload_config");

  // 5. Test FTP
  await mcpTool("classic-mac-hardware", "test_connection", {
    machine: answers.machineId,
    test_launchappl: false
  });

  // 6. Build LaunchAPPLServer
  const buildPlatform = platformToBuild[answers.platform];
  await bash(`./scripts/build-launcher.sh ${buildPlatform}`);

  // 7. Deploy
  const binaryPath = platformToBinaryPath[answers.platform];
  await mcpTool("classic-mac-hardware", "deploy_binary", {
    machine: answers.machineId,
    platform: platformMap[answers.platform],
    binary_path: binaryPath
  });

  // 8. Provide instructions
  console.log(`
    ✓ Machine added: ${answers.machineId}
    ✓ FTP connectivity verified
    ✓ LaunchAPPLServer built and deployed

    Next steps: ...
  `);
}
```

## Files Modified

- `.claude/mcp-servers/classic-mac-hardware/machines.json` - Machine added
- `LaunchAPPL/build-mactcp/Server/LaunchAPPLServer-MacTCP.bin` - Built (if mactcp)
- `LaunchAPPL/build-ppc/Server/LaunchAPPLServer-OpenTransport.bin` - Built (if ot)

## Dependencies

- MCP server must be running (classic-mac-hardware)
- Docker must be available
- Retro68 toolchain must be built in Docker container
- machines.json must exist (or be created)
