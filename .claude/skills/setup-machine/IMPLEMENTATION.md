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

Use **ONE SINGLE AskUserQuestion call** to collect ALL information at once.

**CRITICAL RULES FOR DATA COLLECTION:**
- **Ask ALL questions in a SINGLE AskUserQuestion call** - Don't split into multiple rounds
- **NO clever parsing** - Take user input exactly as given
- **NO assumptions** - If unclear, ask again explicitly after initial collection
- **NO options for text fields** - Only use options for true choices (Platform, Description)
- **Each question should be CLEAR and DIRECT**

**⚠️ CRITICAL: IP ADDRESS INPUT ⚠️**
- IP address question MUST NOT have ANY options - use empty array: `options: []`
- DO NOT provide example IPs as options (like "10.188.1.100" or "192.168.1.100")
- DO NOT ask for IP in multiple parts (no "first 3 octets" then "last octet")
- WHY: If you provide numbered options (1, 2, 3...), typing "1" to start an IP like "10.x.x.x" will select option 1 instead of allowing text input
- User types the COMPLETE IP address in one text input (e.g., "10.188.1.102")
- With empty options array, AskUserQuestion will show ONLY the "Type something" option
- This is a simple text field - user types, you receive the complete IP

**IMPORTANT:** Call AskUserQuestion ONCE with ALL 7 questions. Do NOT make multiple separate calls.

```
Question 1: Machine ID
  Header: "Machine ID"
  Prompt: "What's the machine nickname (e.g., 'performa6400', 'se30', 'iici')?"
  multiSelect: false
  Options: NONE - This is a TEXT INPUT question
  - User types a value (text input via "Other")
  - TAKE EXACTLY AS PROVIDED - no validation yet

Question 2: IP Address
  Header: "IP Address"
  Prompt: "What's the IP address of this Mac? (e.g., 10.188.1.102)"
  multiSelect: false
  Options: [] ← EMPTY ARRAY - ABSOLUTELY NO OPTIONS

  CRITICAL RULES FOR IP ADDRESS:
  - The options array MUST be EMPTY [] - NO example IPs, NO suggestions, NOTHING
  - DO NOT add options like "10.188.1.100" or "192.168.1.100" - this causes option selection conflicts
  - DO NOT ask for IP in parts (no "first 3 octets" then "last octet")
  - WHY: Example IPs like "1. 10.188.1.100" cause problems when user tries to type "10.x.x.x" (the "1" triggers option 1)
  - With empty options, user will ONLY see the "Type something" option for text input
  - User will type the COMPLETE IP in one go (e.g., "10.188.1.102")
  - Take their answer EXACTLY as typed

Question 3: Platform
  Header: "Platform"
  Prompt: "Which networking platform does this Mac use?"
  multiSelect: false
  Options:
    - label: "MacTCP", description: "System 6.0.8 - 7.5.5, 68k Macs (SE/30, IIci, LC)"
    - label: "Open Transport", description: "System 7.6.1+ / Mac OS 8-9, PPC or late 68040 Macs"

Question 4: FTP Username
  Header: "FTP Username"
  Prompt: "What's the FTP username for this Mac?"
  multiSelect: false
  Options: NONE - This is a TEXT INPUT question
  - User types username (text input via "Other")
  - TAKE EXACTLY AS PROVIDED
  - DO NOT try to parse phrases like "mac for both"

Question 5: FTP Password
  Header: "FTP Password"
  Prompt: "What's the FTP password for this Mac?"
  multiSelect: false
  Options: NONE - This is a TEXT INPUT question
  - User types password (text input via "Other")
  - TAKE EXACTLY AS PROVIDED
  - DO NOT try to parse complex answers

Question 6: System Version
  Header: "System Version"
  Prompt: "What's the System version? (e.g., '7.5.3', '7.6.1', '8.1')"
  multiSelect: false
  Options: NONE - This is a TEXT INPUT question
  - User types version (text input via "Other")

Question 7: Description (Optional)
  Header: "Description"
  Prompt: "Optional: Add a description for this machine (or leave blank)"
  multiSelect: false
  Options:
    - label: "Leave blank", description: "No description needed"
    - label: "68k Mac", description: "Generic 68k Macintosh"
    - label: "PPC Mac", description: "Generic PowerPC Macintosh"
  - If user selects "Other", use their exact text as the description
  - TAKE their input EXACTLY as provided
```

**CRITICAL:** When calling AskUserQuestion:
- Pass ALL 7 questions in the `questions` array parameter
- Questions 1, 2, 4, 5, 6 should have NO options (empty array) - they are pure text inputs
- Only Questions 3 and 7 should have options (Platform choice and Description choice)
- User will type into "Other" for text-input questions automatically

**IMPORTANT:** After collecting all answers in the single AskUserQuestion call:
1. Validate each answer
2. If any answer is ambiguous or incomplete (e.g., IP contains "x", username is empty):
   - DO NOT try to parse or infer the intent
   - Ask a NEW follow-up AskUserQuestion with just that ONE specific question
   - Make it COMPLETELY EXPLICIT what you need
   - Example: "I need the complete IP address. Please provide the full IP address (e.g., 10.188.1.102):"
   - Take the new answer EXACTLY as provided

**AskUserQuestion Rules:**
- For text-input questions (Machine ID, IP, Username, Password, System Version):
  - DO NOT provide options array (or provide empty array)
  - User will automatically get "Other" option to type their answer
- For choice questions (Platform, Description):
  - Provide 2-4 options with clear labels and descriptions
  - User can still select "Other" to type custom text
- The tool automatically adds "Other" option - you don't need to include it

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
  - If empty or confusing, ask a follow-up AskUserQuestion with just that specific field

If validation fails, provide clear error messages with examples and re-prompt using a new AskUserQuestion call with just the invalid field(s).

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

**LaunchAPPLServer source is in the Retro68 container** at `/opt/Retro68/LaunchAPPL/Server/`.

Build based on the machine's platform:

```bash
Platform: mactcp       → Build 68k version
Platform: opentransport → Build PPC version
```

**Build commands (run in Docker):**

For **MacTCP (68k)**:
```bash
docker compose -f docker/docker-compose.yml run --rm peertalk-dev bash -c "
  cd /opt/Retro68/LaunchAPPL
  rm -rf build-mactcp
  mkdir -p build-mactcp
  cd build-mactcp
  cmake .. -DCMAKE_TOOLCHAIN_FILE=/opt/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake -DCMAKE_BUILD_TYPE=Release
  make -j\$(nproc)
  # Copy to workspace for deployment
  cp Server/*.bin Server/*.dsk /workspace/
"
```

For **Open Transport (PPC)**:
```bash
docker compose -f docker/docker-compose.yml run --rm peertalk-dev bash -c "
  cd /opt/Retro68/LaunchAPPL
  rm -rf build-ppc
  mkdir -p build-ppc
  cd build-ppc
  cmake .. -DCMAKE_TOOLCHAIN_FILE=/opt/Retro68-build/toolchain/powerpc-apple-macos/cmake/retro68.toolchain.cmake -DCMAKE_BUILD_TYPE=Release
  make -j\$(nproc)
  # Copy to workspace for deployment
  cp Server/*.bin Server/*.dsk /workspace/
"
```

**Expected outputs:**
- MacTCP: `LaunchAPPLServer-MacTCP.bin` and `.dsk` in workspace root
- Open Transport: `LaunchAPPLServer-OpenTransport.bin` and `.dsk` in workspace root

### 8. Deploy LaunchAPPLServer

Map platform to binary paths in workspace root:

```
Platform: mactcp
  bin_path: LaunchAPPLServer-MacTCP.bin
  dsk_path: LaunchAPPLServer-MacTCP.dsk

Platform: opentransport
  bin_path: LaunchAPPLServer-OpenTransport.bin
  dsk_path: LaunchAPPLServer-OpenTransport.dsk
```

Deploy BOTH files using MCP upload_file tool (NOT deploy_binary - that's for PeerTalk builds):

```bash
# Deploy .bin to Applications:LaunchAPPLServer:
mcp__classic-mac-hardware__upload_file
  machine=<machine_id>
  local_path=<bin_path>
  remote_path="Applications:LaunchAPPLServer:<filename>.bin"

# Deploy .dsk to Applications:LaunchAPPLServer:
mcp__classic-mac-hardware__upload_file
  machine=<machine_id>
  local_path=<dsk_path>
  remote_path="Applications:LaunchAPPLServer:<filename>.dsk"
```

### 8.5. Pause for User to Launch LaunchAPPLServer

**IMPORTANT:** Before proceeding, the user must:
1. Go to the Mac and navigate to Applications:LaunchAPPLServer
2. Mount the .dsk file (or use BinUnpk/StuffIt to extract the .bin file)
3. Copy LaunchAPPLServer application to Applications folder
4. Launch LaunchAPPLServer
5. In preferences, enable "TCP Server" on port 1984

**Output to user:**
```
✓ LaunchAPPLServer deployed to Applications:LaunchAPPLServer

NEXT STEP: Before continuing, you need to launch LaunchAPPLServer on your Mac:
1. On your <machine_name>, navigate to Applications:LaunchAPPLServer
2. Mount the .dsk disk image (or extract the .bin file with BinUnpk/StuffIt)
3. Copy LaunchAPPLServer to your Applications folder
4. Launch LaunchAPPLServer
5. In preferences, enable "TCP Server" on port 1984

Press Enter when LaunchAPPLServer is running and ready to test...
```

Wait for user confirmation before continuing to the next step.

### 8.6. Build and Deploy Hello World Test App

Build a simple Hello World app from Retro68 Samples to verify LaunchAPPL works:

**Build Hello World (using same platform as machine):**

For **MacTCP (68k)**:
```bash
docker compose -f docker/docker-compose.yml run --rm peertalk-dev bash -c "
  cd /opt/Retro68/Samples/HelloWorld
  rm -rf build-mactcp
  mkdir -p build-mactcp
  cd build-mactcp
  cmake .. -DCMAKE_TOOLCHAIN_FILE=/opt/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake -DCMAKE_BUILD_TYPE=Release
  make -j\$(nproc)
  cp HelloWorld.bin HelloWorld.dsk /workspace/
"
```

For **Open Transport (PPC)**:
```bash
docker compose -f docker/docker-compose.yml run --rm peertalk-dev bash -c "
  cd /opt/Retro68/Samples/HelloWorld
  rm -rf build-ppc
  mkdir -p build-ppc
  cd build-ppc
  cmake .. -DCMAKE_TOOLCHAIN_FILE=/opt/Retro68-build/toolchain/powerpc-apple-macos/cmake/retro68.toolchain.cmake -DCMAKE_BUILD_TYPE=Release
  make -j\$(nproc)
  cp HelloWorld.bin HelloWorld.dsk /workspace/
"
```

**Deploy Hello World to test folder:**

```bash
mcp__classic-mac-hardware__upload_file
  machine=<machine_id>
  local_path="HelloWorld.bin"
  remote_path="Temp:HelloWorld.bin"

mcp__classic-mac-hardware__upload_file
  machine=<machine_id>
  local_path="HelloWorld.dsk"
  remote_path="Temp:HelloWorld.dsk"
```

### 9. Provide User Instructions

Output clear next steps for the user:

```
✓ Machine added: <machine_id>
✓ FTP connectivity verified
✓ Directory structure created
✓ LaunchAPPLServer built for <platform>
✓ LaunchAPPLServer deployed (.bin and .dsk)
✓ Hello World test app built and deployed

Next steps on your <machine_name>:
1. Navigate to Applications:LaunchAPPLServer folder
2. Mount the LaunchAPPLServer-<Platform>.dsk disk image
   (or use StuffIt/BinUnpk to extract the .bin file)
3. Copy LaunchAPPLServer to your Applications folder
4. Launch LaunchAPPLServer
5. In preferences, enable "TCP Server" on port 1984
6. Test connectivity: /test-machine <machine_id>

Test the Hello World app:
1. Navigate to Temp folder
2. Mount HelloWorld.dsk (or extract HelloWorld.bin)
3. Double-click HelloWorld application
4. You should see a "Hello, World!" window

Once LaunchAPPL connectivity is verified, you can:
  /deploy <machine_id> <platform>   # Deploy PeerTalk builds
  /fetch-logs <machine_id>          # Retrieve PT_Log output
  /implement <session>              # Start development work
```

## Error Handling

### Unclear User Input
**NEVER try to parse or infer unclear answers. Instead:**
- After the initial AskUserQuestion call, validate all answers
- If IP is incomplete (like "10.188.1.x" or empty):
  - Call AskUserQuestion again with JUST the IP question
  - Make it explicit: "I need the complete IP address. Please provide the full IP address (e.g., 10.188.1.102 or 192.168.1.50)"
  - Use empty options array (text input only)
- If username or password is empty/ambiguous:
  - Call AskUserQuestion again with JUST that specific question
  - Be explicit about what you need
- Take each answer EXACTLY as typed - no clever parsing
- Continue validating and re-asking until you have complete, valid data

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

  // 1. Collect ALL info in ONE call
  const answers = await askUserQuestions([
    {
      question: "What's the machine nickname (e.g., 'performa6400', 'se30')?",
      header: "Machine ID",
      options: [],  // Empty = text input
      multiSelect: false
    },
    {
      question: "What's the IP address of this Mac? (e.g., 10.188.1.102)",
      header: "IP Address",
      options: [],  // CRITICAL: MUST be empty! DO NOT add example IPs like "10.188.1.100" - causes conflicts!
      multiSelect: false
    },
    {
      question: "Which networking platform does this Mac use?",
      header: "Platform",
      options: [
        { label: "MacTCP", description: "System 6.0.8 - 7.5.5, 68k Macs" },
        { label: "Open Transport", description: "System 7.6.1+, PPC/68040" }
      ],
      multiSelect: false
    },
    {
      question: "What's the FTP username for this Mac?",
      header: "FTP Username",
      options: [],  // Empty = text input
      multiSelect: false
    },
    {
      question: "What's the FTP password for this Mac?",
      header: "FTP Password",
      options: [],  // Empty = text input
      multiSelect: false
    },
    {
      question: "What's the System version? (e.g., '7.5.3', '7.6.1', '8.1')",
      header: "System Version",
      options: [],  // Empty = text input
      multiSelect: false
    },
    {
      question: "Optional: Add a description for this machine (or leave blank)",
      header: "Description",
      options: [
        { label: "Leave blank", description: "No description needed" },
        { label: "68k Mac", description: "Generic 68k Macintosh" },
        { label: "PPC Mac", description: "Generic PowerPC Macintosh" }
      ],
      multiSelect: false
    }
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
