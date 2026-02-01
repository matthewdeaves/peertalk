# Classic Mac Hardware MCP Server - Setup Guide

Guide for configuring your personal Classic Mac test machines.

## Quick Start (First Time Setup)

**ONE command to set up everything:**

```bash
./tools/setup.sh
```

This single command:
- ✓ Installs host dependencies (jq, python3)
- ✓ Checks Docker is installed
- ✓ Builds/pulls the Docker development image
- ✓ Configures MCP server (creates .mcp.json)

Then **restart Claude Code completely** (exit and reopen) to load MCP servers.

After restart:

```bash
/test-machine --list              # Verify MCP is working
/setup-machine                    # Register your Classic Macs
/setup-launcher <machine-id>      # Deploy LaunchAPPLServer
```

**Requirements:**
- Docker (install from https://docs.docker.com/get-docker/)
- That's it! Everything else is automatic.

---

## For Project Users

Each developer has different Classic Mac hardware with different IP addresses and FTP credentials. The `machines.json` file is **user-specific** and **NOT committed to git**.

### Detailed Setup Steps

The `./tools/setup.sh` script performs these steps automatically:

```bash
# 1. Install host dependencies (jq, python3)
# 2. Create Python virtual environment
# 3. Build/pull Docker container
./scripts/docker-build.sh

# 4. Configure MCP server
cp .mcp.json.example .mcp.json

# 5. RESTART Claude Code completely (required to load MCP)
#    Exit and reopen, not just reload

# 6. Verify MCP is working
/test-machine --list   # Should show "No machines configured yet"

# 7. Add your Classic Macs
/setup-machine
```

**Why restart?** Claude Code only loads MCP server configurations on startup, not dynamically.

**Manual setup:** You can run individual scripts if needed, but `./tools/setup.sh` does it all.

### Adding a New Machine

After MCP is configured, use a two-step process to set up each Classic Mac:

**Step 1: Register the machine**
```bash
/setup-machine
```

This interactive skill will:
1. Collect machine details (IP, platform, credentials)
2. Add to `.claude/mcp-servers/classic-mac-hardware/machines.json`
3. Verify FTP connectivity
4. Create directory structure on Mac via FTP

**Step 2: Deploy LaunchAPPLServer**
```bash
/setup-launcher <machine-id>
```

This skill will:
1. Build the correct LaunchAPPLServer version for your platform
2. Deploy LaunchAPPLServer files (.bin and .dsk) via FTP
3. Guide you through final setup on the Mac (using BinUnpk to extract)

### RumpusFTP Server Setup

**On each Classic Mac:**

1. Install RumpusFTP server (compatible with System 6+)
2. Configure FTP server:
   - Enable passive mode (PASV) - required for NAT traversal
   - Set port to 21 (standard)
   - Create user account (e.g., "mac")
3. Ensure FTP user has read/write permissions to create directories

**Note:** The `/setup-machine` skill will create the required directory structure automatically:
- `Applications:PeerTalk` - for binaries
- `Applications:LaunchAPPLServer` - for LaunchAPPLServer app
- `Documents:PeerTalk-Logs` - for log files
- `Temp` - for temporary files

### Configuration File

**machines.json** format (created automatically by /setup-machine):

```json
{
  "se30": {
    "name": "SE/30",
    "platform": "mactcp",
    "system": "System 6.0.8",
    "cpu": "68030",
    "ftp": {
      "host": "192.168.1.10",
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
    "notes": "SE/30 with 8MB RAM"
  }
}
```

**Note:** Paths use Mac-style colons (`:`) for subdirectories, not slashes.

### Credentials

FTP credentials are stored directly in machines.json (which is gitignored). Since Classic Mac FTP is unencrypted anyway and runs on a local network, there's no benefit to environment variables - just use plain credentials.

```json
"ftp": {
  "username": "mac",
  "password": "mac"
}
```

### Testing the Connection

Use the MCP server's built-in connection test:

```bash
# Via Claude Code (preferred - uses MCP server)
/test-connection se30

# Or use MCP tool directly
mcp__classic-mac-hardware__test_connection machine=se30
```

This tests both FTP and LaunchAPPL connectivity.

**Manual FTP test:**
```bash
ftp -n 192.168.1.10
# user peertalk your-password
# passive
# ls
# quit
```

### LaunchAPPLServer Setup (Remote Execution)

For remote binary execution, install LaunchAPPLServer on each Classic Mac.

**Automatic Setup (Recommended):**

After running `/setup-machine` and `/setup-launcher`:

1. **On your Classic Mac**, navigate to `Applications:LaunchAPPLServer` folder
2. **Mount the disk image** by double-clicking `LaunchAPPLServer.dsk`
   (Or use **BinUnpk** to extract from the .bin file)
3. **Copy LaunchAPPLServer** to your Applications folder
4. **Launch** the LaunchAPPLServer application
5. **Configure:**
   - Enable TCP server
   - Set port to 1984 (default)
   - Leave running
6. **Test connectivity:**
   ```bash
   /test-machine your-machine-id
   ```

**Manual Build & Deploy:**

If you need to rebuild LaunchAPPLServer separately:

1. **Build and deploy in one step:**
   ```bash
   /setup-launcher your-machine-id
   ```

2. **Or build manually** (platform-specific):
   ```bash
   # MacTCP (68k, System 6.0.8 - 7.5.5)
   ./scripts/build-launcher.sh mactcp

   # Open Transport (PPC, System 7.6.1+)
   ./scripts/build-launcher.sh ot
   ```

   Then deploy using MCP tools (see `/setup-launcher` skill for details).

3. **Follow setup steps above** (mount .dsk or use BinUnpk to extract)

## For Project Maintainers

When sharing this project or creating the starter-template branch:

1. **Include:**
   - `machines.example.json` - Template with example configuration
   - `setup.sh` - Interactive setup script
   - `README.md` - Documentation
   - `SETUP.md` - This setup guide

2. **DO NOT include:**
   - `machines.json` - User-specific, contains credentials
   - `.env` - User-specific environment variables

3. **Update .gitignore:**
   ```
   .claude/mcp-servers/*/machines.json
   .env
   ```

## Network Configuration

### Static IP Addresses (Recommended)

Assign static IPs to Classic Macs for consistent configuration:

```
SE/30 (MacTCP):       192.168.1.10
IIci (Open Transport): 192.168.1.11
Quadra (AppleTalk):    192.168.1.12
```

### Dynamic IP (Alternative)

If using DHCP:
- Configure router to assign fixed DHCP leases based on MAC address
- Or use Bonjour/mDNS if available (less reliable on System 6)

### Firewall Rules

Open port 21 (FTP control) and passive port range (e.g., 49152-65535):

```bash
# Example: ufw on Linux
sudo ufw allow 21/tcp
sudo ufw allow 49152:65535/tcp
```

## Troubleshooting

### "Connection refused"

- Check Classic Mac is powered on and RumpusFTP is running
- Verify IP address in machines.json matches Classic Mac
- Ping the Classic Mac: `ping 192.168.1.10`

### "Login incorrect"

- Verify username/password in machines.json
- Check environment variable is set: `echo $PEERTALK_SE30_FTP_PASSWORD`
- Test with command-line FTP client

### "Passive mode failed"

- Enable passive mode in RumpusFTP server settings
- Check firewall allows passive port range
- Verify NAT/router doesn't block passive FTP

### "Permission denied"

- Check FTP user has read/write permissions on directories
- Verify paths in machines.json exist on Classic Mac
- Check Mac file permissions (not locked, not in use)

## Multiple Developers

When multiple developers work on the project:

1. Each developer configures MCP: `cp .mcp.json.example .mcp.json`
2. Each uses `/setup-machine` to add their Classic Mac hardware
3. `machines.json` is never shared (gitignored)
4. Only `machines.example.json` is version-controlled

**Example team setup:**

```
Developer A:
  SE/30:       192.168.1.10 (mactcp)
  Performa:    192.168.1.11 (opentransport)

Developer B:
  IIci:        192.168.2.20 (mactcp)
  PowerMac:    192.168.2.21 (opentransport)
```

Each has different IPs, different machine combinations - all work seamlessly.

## Security Notes

1. **FTP is unencrypted** - Classic Macs don't support SFTP, so this is unavoidable
2. **Credentials in machines.json** - File is gitignored, and FTP is unencrypted anyway
3. **Local network only** - Keep Classic Macs on a trusted local network

## See Also

- [MCP Server README](README.md) - Full server documentation
- [RumpusFTP Documentation](http://www.stairways.com/action/support) - FTP server for Classic Mac
- [Classic Mac Networking Guide](../../docs/CLASSIC-MAC-NETWORKING.md) - Network setup guide
