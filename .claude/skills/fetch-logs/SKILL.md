---
name: fetch-logs
description: Retrieve PT_Log output from Classic Mac test machines via FTP. Uses classic-mac-hardware MCP server. Supports fetching from single machine, platform (all machines), or all machines. Optionally compare logs side-by-side.
argument-hint: [machine-id|platform|all] [--compare]
---

# Fetch Logs from Classic Mac Hardware

Retrieve PT_Log output from Classic Mac test machines via FTP.

## Usage

```bash
/fetch-logs se30                   # Get latest logs from SE/30
/fetch-logs se30 session-12345     # Get specific session
/fetch-logs mactcp                 # Get logs from all MacTCP machines
/fetch-logs all                    # Get logs from all machines
/fetch-logs --compare              # Fetch all and compare side-by-side
/fetch-logs se30 --tail 50         # Show last 50 lines only
```

## Prerequisites

1. **Machines configured:**
   - Check `.claude/mcp-servers/classic-mac-hardware/machines.json`
   - Or use MCP server: `/mcp` → list resources

2. **Logs exist on machine:**
   - PT_Log writes to `Documents:PeerTalk-Logs`
   - Format: `session-{timestamp}.log`

3. **MCP server running**

## Commands

### Fetch from Single Machine

```bash
/fetch-logs se30
```

**Process:**
1. Connect to SE/30 via FTP
2. List files in logs directory
3. Get most recent `.log` file
4. Download and display

**Output:**
```
Fetching logs from SE/30
========================

Session: session-2026-02-01-143215.log
Size: 12.4 KB
Downloaded: ✓

Logs:
─────────────────────────────────────────────────────────────
[2026-02-01 14:32:15] [CORE] [INFO] PeerTalk initialized
[2026-02-01 14:32:15] [CORE] [INFO] Platform: MacTCP
[2026-02-01 14:32:15] [MACTCP] [INFO] TCP stream opened
[2026-02-01 14:32:16] [MACTCP] [INFO] Connection established
[2026-02-01 14:32:16] [CORE] [INFO] Sent 512 bytes to peer
[2026-02-01 14:32:17] [CORE] [INFO] Received 512 bytes from peer
[2026-02-01 14:32:17] [CORE] [INFO] Test passed
─────────────────────────────────────────────────────────────

Summary:
  Total lines: 145
  INFO: 142
  WARN: 2
  ERROR: 1

Error details:
  Line 89: [MACTCP] [ERROR] TCPSend returned -3271

Saved to: logs/se30/session-2026-02-01-143215.log
```

### Fetch Specific Session

```bash
/fetch-logs se30 session-2026-02-01-120000
```

**Gets specific log file instead of latest:**
```
Fetching logs from SE/30
========================

Session: session-2026-02-01-120000.log
...
```

### Fetch from All Machines of Platform

```bash
/fetch-logs mactcp
```

**Gets logs from all MacTCP machines:**
```
Fetching logs from MacTCP machines
===================================

Found 2 MacTCP machines:
  - se30 (SE/30)
  - classic (Classic II)

se30 (SE/30):
  Session: session-2026-02-01-143215.log
  Size: 12.4 KB
  Downloaded: ✓
  Saved to: logs/se30/session-2026-02-01-143215.log

classic (Classic II):
  Session: session-2026-02-01-143220.log
  Size: 8.1 KB
  Downloaded: ✓
  Saved to: logs/classic/session-2026-02-01-143220.log

✓ Fetched logs from 2/2 machines
```

### Fetch from All Machines

```bash
/fetch-logs all
```

**Gets logs from all configured machines:**
```
Fetching logs from all machines
================================

MacTCP:
  se30 (SE/30):      ✓ (12.4 KB)
  classic (Classic): ✓ (8.1 KB)

Open Transport:
  iici (IIci):       ✓ (15.2 KB)

AppleTalk:
  quadra (Quadra):   ✓ (9.8 KB)

✓ Fetched logs from 4/4 machines
All logs saved to logs/{machine}/
```

### Compare Logs

```bash
/fetch-logs --compare
```

**Fetches all logs and displays side-by-side comparison:**
```
Fetching and comparing logs
============================

Downloading...
  se30:   ✓
  iici:   ✓
  quadra: ✓

Comparing logs (first 20 lines):

Line | POSIX              | se30 (MacTCP)      | iici (OT)          | Status
─────┼────────────────────┼────────────────────┼────────────────────┼────────
1    | PeerTalk init      | PeerTalk init      | PeerTalk init      | ✓ Match
2    | Platform: POSIX    | Platform: MacTCP   | Platform: OT       | Expected
3    | TCP stream opened  | TCP stream opened  | Endpoint opened    | Expected
4    | Connect to peer    | Connect to peer    | Connect to peer    | ✓ Match
5    | Connected          | Connected          | Connected          | ✓ Match
6    | Sent 512 bytes     | Sent 512 bytes     | Sent 512 bytes     | ✓ Match
7    | Received 512 bytes | ERROR: -3271       | Received 512 bytes | ✗ DIVERGE
─────┴────────────────────┴────────────────────┴────────────────────┴────────

Divergence detected at line 7!

POSIX:  [INFO] Received 512 bytes
se30:   [ERROR] TCPSend failed: -3271 (connectionClosing)
iici:   [INFO] Received 512 bytes

Recommendation: Investigate MacTCP error -3271
  Use /mac-api to look up error code
  Or say "debug crash on se30" to spawn cross-platform-debug agent
```

### Show Tail Only

```bash
/fetch-logs se30 --tail 50
```

**Shows last 50 lines only (useful for large logs):**
```
Fetching logs from SE/30 (last 50 lines)
=========================================

[2026-02-01 14:35:42] [CORE] [INFO] Test 47 passed
[2026-02-01 14:35:43] [CORE] [INFO] Test 48 passed
[2026-02-01 14:35:44] [CORE] [INFO] Test 49 passed
[2026-02-01 14:35:45] [CORE] [INFO] Test 50 passed
[2026-02-01 14:35:45] [CORE] [INFO] All tests passed

Summary: 50 lines shown (345 lines total)
```

## Process

### Step 1: Parse Arguments

```
$ARGUMENTS = "se30"                → machine="se30", session="latest"
$ARGUMENTS = "se30 session-123"    → machine="se30", session="session-123"
$ARGUMENTS = "mactcp"              → platform="mactcp"
$ARGUMENTS = "all"                 → fetch all
$ARGUMENTS = "--compare"           → fetch all + compare
$ARGUMENTS = "se30 --tail 50"      → tail=50
```

### Step 2: Fetch Logs via MCP Server

**Use MCP resource:**
```python
import classic_mac_hardware as mac

# Get latest logs
logs = mac.get_resource('mac://se30/logs/latest')

# Get specific session
logs = mac.get_resource('mac://se30/logs/session-2026-02-01-120000')
```

**Or call MCP tool:**
```
Tool: fetch_logs
Arguments:
  machine: se30
  session_id: latest
  destination: logs/se30/latest.log
```

### Step 3: Parse PT_Log Format

**PT_Log format:**
```
[timestamp] [category] [level] message
```

**Example:**
```
[2026-02-01 14:32:15] [MACTCP] [ERROR] TCPSend failed: -3271
```

**Parse to extract:**
- `timestamp`: 2026-02-01 14:32:15
- `category`: MACTCP
- `level`: ERROR
- `message`: TCPSend failed: -3271

### Step 4: Analyze Logs

**Count by level:**
```python
info_count = logs.count('[INFO]')
warn_count = logs.count('[WARN]')
error_count = logs.count('[ERROR]')
```

**Extract errors:**
```python
error_lines = [line for line in logs.split('\n') if '[ERROR]' in line]
```

**Find patterns:**
- First error
- Last error
- Error frequency
- Category distribution

### Step 5: Save Locally

```bash
mkdir -p logs/{machine}/
echo "$logs" > logs/{machine}/session-{timestamp}.log
```

**Directory structure:**
```
logs/
├── se30/
│   ├── session-2026-02-01-143215.log
│   ├── session-2026-02-01-120000.log
│   └── latest.log -> session-2026-02-01-143215.log
├── iici/
│   └── session-2026-02-01-143220.log
└── posix/
    └── latest.log
```

### Step 6: Display Results

**Show:**
- Session ID
- File size
- Line count
- Summary (INFO/WARN/ERROR counts)
- First few errors
- Where saved locally

**For comparison:**
- Align logs by line number
- Highlight differences
- Note divergence point
- Suggest next steps

## PT_Log Categories

Based on source code structure:

| Category | Source |
|----------|--------|
| CORE | Core PeerTalk logic |
| MACTCP | MacTCP-specific |
| OT | Open Transport-specific |
| APPLETALK | AppleTalk-specific |
| POSIX | POSIX-specific |
| LOG | Logging system itself |

## Log Levels

| Level | Meaning |
|-------|---------|
| ERROR | Operation failed, needs attention |
| WARN | Potential issue, may be recoverable |
| INFO | Normal operation, informational |
| DEBUG | Detailed diagnostic info |

## Error Handling

### No Logs Found

```
✗ No logs found on se30

Possible causes:
  1. Binary hasn't been run yet
  2. PT_Log not initialized
  3. Logs directory doesn't exist

Try:
  - Run binary on Classic Mac first
  - Check logs directory: Documents:PeerTalk-Logs
  - Verify PT_Log initialization in code
```

### Connection Failed

```
✗ Connection failed: se30 (192.168.1.10)

Troubleshooting:
  - Verify machines.json has correct IP/credentials
  - Test with: /deploy se30 mactcp --dry-run
```

### Permission Denied

```
✗ Permission denied: Documents:PeerTalk-Logs

Fix:
  - Check FTP user has read permissions
  - Verify directory exists on Classic Mac
  - Check directory isn't locked
```

### Log File Corrupted

```
⚠ Warning: Log file may be corrupted
  Lines parsed: 142/145
  Invalid lines: 3

Proceeding with partial data...
```

## Integration

### With `/deploy` Skill

```bash
/deploy se30 mactcp         # Deploy binary
# Test on SE/30
/fetch-logs se30            # Get results
```

### With cross-platform-debug Agent

```bash
/fetch-logs --compare       # Compare logs
# Notice divergence
"debug crash on se30"       # Agent auto-spawns, analyzes
```

### With `/hw-test` Skill

```bash
/hw-test generate 5.3       # Create test plan
/deploy se30 mactcp         # Deploy
# Follow test plan on SE/30
/fetch-logs se30            # Get test results
# Update test plan with results
```

### With Local POSIX Logs

```bash
# Compare Classic Mac logs with POSIX
/fetch-logs se30
diff logs/se30/latest.log logs/posix/latest.log
```

## Advanced Usage

### Fetch and Parse

```bash
/fetch-logs se30 --parse-errors
# Extracts all errors and looks up error codes via /mac-api
```

### Fetch and Filter

```bash
/fetch-logs se30 --filter MACTCP
# Shows only MACTCP category logs
```

### Fetch Multiple Sessions

```bash
/fetch-logs se30 --last 5
# Fetches last 5 sessions from SE/30
```

### Export Format

```bash
/fetch-logs se30 --format json
# Converts to JSON for programmatic analysis
```

## Comparison Algorithms

### Simple Diff

Line-by-line comparison, exact match only.

### Smart Diff

- Ignores timestamps (expected to differ)
- Ignores platform names (POSIX vs MacTCP)
- Focuses on operation sequences
- Highlights error divergence

### Sequence Alignment

- Uses sequence alignment algorithm
- Finds best match even with insertions/deletions
- Shows where logs diverge semantically

## Workflow Examples

### Quick Test Check

```bash
# After testing on SE/30
/fetch-logs se30 --tail 20
# Quick check of recent activity
```

### Cross-Platform Validation

```bash
/fetch-logs --compare
# See how all platforms behaved
# Identify platform-specific issues
```

### Error Investigation

```bash
/fetch-logs se30
# Notice error at line 89
/mac-api what does error -3271 mean?
# Look up error code
# Fix code based on findings
```

### Historical Analysis

```bash
/fetch-logs se30 --last 10
# Get last 10 sessions
# Track error frequency over time
# See if issue is consistent or intermittent
```

## Notes

- Logs are cached locally in `logs/{machine}/`
- Latest log symlinked for convenience
- Large logs (>1MB) show progress during download
- Binary mode FTP for reliable transfer
- PT_Log format is consistent across platforms (by design)
- Compare feature requires all machines reachable

## See Also

- `/deploy` - Deploy binaries to machines
- `/mac-api` - Look up error codes
- `/hw-test` - Hardware test plans
- cross-platform-debug agent - Auto-triggered debugging
- [MCP Server README](../../mcp-servers/classic-mac-hardware/README.md)
- [machines.json](../../mcp-servers/classic-mac-hardware/machines.json) - Machine configuration
