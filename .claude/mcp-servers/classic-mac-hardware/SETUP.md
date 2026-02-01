# Classic Mac Hardware MCP Server - Setup Guide

Guide for configuring your personal Classic Mac test machines.

## For Project Users

Each developer has different Classic Mac hardware with different IP addresses and FTP credentials. The `machines.json` file is **user-specific** and **NOT committed to git**.

### Quick Setup

**Everything runs in Docker - zero host dependencies!**

```bash
# 1. Build Docker container (one-time, ~2GB download or 30-60 min build)
./scripts/docker-build.sh

# 2. Configure your machines
cd .claude/mcp-servers/classic-mac-hardware
cp machines.example.json machines.json
# Edit machines.json with your FTP details

# 3. Configure Claude Code MCP
cd ../../..
cp .mcp.json.example .mcp.json

# Done! Start a new Claude Code session and the MCP server will run automatically in Docker
```

**Host Requirements:**
- Docker
- That's it! Python/MCP dependencies are in the container.

### RumpusFTP Server Setup

**On each Classic Mac:**

1. Install RumpusFTP server (compatible with System 6+)
2. Configure FTP server:
   - Enable passive mode (PASV) - required for NAT traversal
   - Set port to 21 (standard)
   - Create user account (e.g., "peertalk")
3. Create directories:
   - `/Applications/PeerTalk/` - for binaries
   - `/Documents/PeerTalk-Logs/` - for log files
   - `/Temp/` - for temporary files
4. Set permissions for FTP user to read/write these directories

### Configuration File

**machines.json** format:

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
      "username": "peertalk",
      "password": "${PEERTALK_SE30_FTP_PASSWORD}",
      "paths": {
        "binaries": "/Applications/PeerTalk/",
        "logs": "/Documents/PeerTalk-Logs/",
        "temp": "/Temp/"
      }
    }
  }
}
```

### Environment Variables

Store FTP passwords in environment variables (more secure than hardcoding):

```bash
# Add to ~/.bashrc or ~/.zshrc
export PEERTALK_SE30_FTP_PASSWORD="your-password"
export PEERTALK_IICI_FTP_PASSWORD="your-password"

# Or use a .env file (also gitignored)
echo "PEERTALK_SE30_FTP_PASSWORD=your-password" >> .env
source .env
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

For remote binary execution, install LaunchAPPLServer on each Classic Mac:

1. **Build LaunchAPPLServer** (two versions needed):
   ```bash
   # MacTCP-only (for System 7.5.3 and earlier)
   /build launchappl-mactcp

   # Open Transport (for System 7.6.1+)
   /build launchappl-ot
   ```

2. **Deploy to Classic Macs:**
   - Use MCP server to upload .dsk and .bin files
   - Mount .dsk file on Classic Mac
   - Copy LaunchAPPLServer app to /Applications/

3. **Configure LaunchAPPLServer:**
   - Launch the app
   - Enable TCP server
   - Set port to 1984 (default)
   - Leave running

4. **Update machines.json:**
   ```json
   {
     "se30": {
       "launchappl": {
         "enabled": true,
         "port": 1984
       }
     }
   }
   ```

5. **Test remote execution:**
   ```bash
   /test-hardware se30
   ```

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

1. Each developer runs `setup.sh` on their workstation
2. Each configures their own Classic Mac hardware
3. `machines.json` is never shared (gitignored)
4. Only `machines.example.json` is version-controlled

**Example team setup:**

```
Developer A:
  SE/30:   192.168.1.10
  IIci:    192.168.1.11

Developer B:
  SE/30:   192.168.2.20
  Quadra:  192.168.2.21

Developer C:
  IIci:    10.0.1.5
  Quadra:  10.0.1.6
```

Each has different IPs, different machine combinations - all work seamlessly.

## Security Notes

1. **FTP is unencrypted** - Use only on trusted local networks
2. **Passwords in environment** - Better than hardcoding, use secrets manager for production
3. **Network isolation** - Classic Macs should be on isolated VLAN/subnet if possible
4. **Read-only where possible** - Configure FTP user with minimal permissions

## See Also

- [MCP Server README](README.md) - Full server documentation
- [RumpusFTP Documentation](http://www.stairways.com/action/support) - FTP server for Classic Mac
- [Classic Mac Networking Guide](../../docs/CLASSIC-MAC-NETWORKING.md) - Network setup guide
