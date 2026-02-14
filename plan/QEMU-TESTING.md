# QEMU-Based Classic Mac Testing for PeerTalk

## Goal
Enable automated PeerTalk testing in QEMU VMs as a **first-class alternative** to real hardware.
- Users without Classic Mac hardware can still run full networking tests
- CI can run tests without physical machines
- Same MCP infrastructure (`execute_binary`) works for both real and virtual Macs

## Current State

### Real Hardware Flow (Working)
```
Docker LaunchAPPL → TCP:1984 → LaunchAPPLServer (Mac) → Test runs → Logs stream to perf_partner
```
- Uses existing MCP `execute_binary` tool
- LaunchAPPLServer pre-installed on Mac, listens on port 1984
- Test logs stream back via PeerTalk network to perf_partner

### Retro68 minivmac Flow (Reference)
```
LaunchAPPL → Creates HFS disk with System+AutoQuit+TestApp → minivmac boots → AutoQuit runs app → App writes "out" file → LaunchAPPL reads "out" from disk
```
- **No networking** - Output via disk file only
- Works for unit tests but can't test PeerTalk networking
- System 6.0.8 base image (~800KB) already exists

### QemuMac Setup (Available)
- 68k Quadra 800 VMs with `dp83932` network interface (Dayna Communicard emulation)
- QEMU user-mode networking (NAT with port forwarding)
- Shared disk system for file transfer
- System 7.6.1 installed on existing VM

## Proposed Architecture: QEMU as MCP Machine Type

**Key Design:** Register QEMU VMs in `machines.json` alongside real Macs. The MCP server handles both transparently.

```
┌─────────────────────────────────────────────────────────────────────┐
│  machines.json                                                       │
│  ├── performa6200  (type: real, ip: 10.188.1.213)                   │
│  ├── macse         (type: real, ip: 10.188.1.55)                    │
│  └── qemu-753      (type: qemu, image: peertalk-753.qcow2)    ← NEW │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│  MCP execute_binary(machine="qemu-753", ...)                         │
│                                                                      │
│  1. Start QEMU headless (if not running)                            │
│  2. Wait for LaunchAPPLServer on localhost:1984                     │
│  3. Transfer & execute binary via LaunchAPPL client                 │
│  4. Return output (same as real hardware)                           │
└─────────────────────────────────────────────────────────────────────┘
```

### QEMU VM Image Contents
- **Mac OS 7.5.3** (good balance of features vs size)
- **MacTCP 2.0.6** configured with DHCP
- **LaunchAPPLServer** listening on port 1984
- **dp83932 driver** (Dayna Communicard for QEMU's emulated NIC)

### Test Execution Flow
```
execute_binary(machine="qemu-753", binary_path="build/mac/test_mactcp.bin")
    │
    ▼
MCP detects type=qemu → starts QEMU with port forwarding 1984:1984
    │
    ▼
LaunchAPPL connects to localhost:1984 → transfers binary
    │
    ▼
LaunchAPPLServer executes test_mactcp → test runs in VM
    │
    ▼
Test discovers perf_partner via UDP broadcast → streams logs
    │
    ▼
Output returns via LaunchAPPL → MCP returns result
```

### Networking
```
Host Network
├── Docker: perf_partner (ports 7353-7355)
├── QEMU user-mode networking
│   ├── VM IP: 10.0.2.15 (QEMU default)
│   └── Port forward: host:1984 → guest:1984
└── VM → Host via QEMU gateway (10.0.2.2)
```

**UDP Broadcast Challenge:** QEMU user-mode networking doesn't forward broadcast.
- **Solution:** perf_partner on host binds to 0.0.0.0, VM broadcasts to 10.0.2.2:7353
- Or configure static peer discovery in test apps for QEMU mode

## Implementation Steps

### Phase 1: Create System 7.5.3 QEMU Image (Manual, One-Time)
1. Use existing QemuMac infrastructure to create new VM
2. Install Mac OS 7.5.3 from Apple Legacy Recovery CD
3. Install MacTCP 2.0.6 (from books/ or archive.org)
4. Configure MacTCP: DHCP or manual IP
5. Build & install LaunchAPPLServer (from Retro68)
6. Test connectivity: VM ↔ host ping
7. Snapshot clean image as `peertalk-753.qcow2`

**Manual steps on Mac:**
- Control Panel → MacTCP → Set Obtain Address: Server (DHCP)
- Launch LaunchAPPLServer → Enable TCP Server → Port 1984
- Test: From host, `telnet localhost 1984` should connect

### Phase 2: Update MCP Server for QEMU Support
Modify `.claude/mcp-servers/classic-mac-hardware/server.py`:

1. **Extend machines.json schema** for QEMU machines:
   ```json
   {
     "qemu-753": {
       "type": "qemu",
       "description": "QEMU System 7.5.3 (MacTCP)",
       "platform": "mactcp",
       "image": "peertalk-753.qcow2",
       "launchappl_port": 1984
     }
   }
   ```

2. **Add QEMU lifecycle management:**
   - `start_qemu_vm(machine_id)` - Start QEMU headless
   - `stop_qemu_vm(machine_id)` - Graceful shutdown
   - `wait_for_launchappl(machine_id, timeout=60)` - Poll TCP:1984

3. **Update `execute_binary` tool:**
   ```python
   if machine["type"] == "qemu":
       start_qemu_vm(machine_id)
       wait_for_launchappl(machine_id)
       # LaunchAPPL connects to localhost:1984
   ```

### Phase 3: Docker Integration
1. **Update `docker/Dockerfile`** to include QEMU m68k:
   ```dockerfile
   RUN apt-get install -y qemu-system-m68k xvfb
   ```

2. **Create `docker/qemu/` directory:**
   - `peertalk-753.qcow2` - Base VM image
   - `800.ROM` - Quadra ROM (auto-downloaded)
   - `run-qemu-headless.sh` - QEMU wrapper with xvfb

3. **Update docker-compose.yml** (optional CI mode):
   ```yaml
   services:
     qemu-mac:
       image: peertalk-dev
       command: ./docker/qemu/run-qemu-headless.sh
       ports:
         - "1984:1984"  # LaunchAPPL
         - "7353:7353/udp"  # PeerTalk discovery
   ```

### Phase 4: Skills & Documentation
1. **New `/setup-qemu-mac` skill:**
   - Downloads System 7.5.3 installer
   - Guides user through VM creation
   - Installs MacTCP + LaunchAPPLServer

2. **Update existing skills:**
   - `/run-test` - Auto-detect QEMU vs real hardware
   - `/test-machine` - Support QEMU connectivity test

3. **Documentation:**
   - Update CLAUDE.md with QEMU section
   - Add `docker/qemu/README.md`

## Files to Create/Modify

### New Files
```
docker/
  qemu/
    run-qemu-headless.sh        # Start QEMU with xvfb, port forwarding
    peertalk-753.qcow2          # Base VM image (user creates manually)
    800.ROM                     # Quadra ROM (auto-downloaded)
    README.md                   # Setup instructions

.claude/
  skills/setup-qemu-mac/        # Skill for VM creation
    prompt.md
  mcp-servers/classic-mac-hardware/
    qemu.py                     # QEMU lifecycle management (new module)
```

### Modified Files
```
.claude/mcp-servers/classic-mac-hardware/
  server.py                     # Add QEMU support to execute_binary
  machines.example.json         # Add QEMU machine example

docker/
  Dockerfile                    # Add qemu-system-m68k, xvfb

CLAUDE.md                       # Document QEMU testing option

.claude/skills/run-test/
  prompt.md                     # Support QEMU machines

.claude/skills/test-machine/
  prompt.md                     # Support QEMU connectivity test
```

## Risk Assessment

| Risk | Mitigation |
|------|------------|
| QEMU networking unreliable | Test thoroughly; QEMU supplements real hardware |
| MacTCP behavior differs from real Mac | Document differences; use QEMU for dev, real for validation |
| UDP broadcast not working in QEMU user-mode | Configure static peer IP (10.0.2.2) as fallback |
| Slow boot times | Keep VM running between tests; snapshot for quick reset |
| dp83932 driver issues | System 7.5.3 should have Ethernet Manager support |

## Verification Plan

### Step 1: VM Creation & Basic Network
```bash
# Boot QEMU manually first
./QemuMac/run-mac.sh --config vms/68k_quadra_800_os753/...

# From within Mac: Open MacTCP control panel
# Verify IP address assignment (DHCP or manual)
# Test: MacTCP Ping (if available) or SimplePing
```

### Step 2: LaunchAPPL Connectivity
```bash
# From host, test port 1984
telnet localhost 1984
# Should connect (then close - no valid protocol message)

# Test with actual LaunchAPPL client
docker run --rm -v $(pwd):/workspace peertalk-dev \
    /opt/Retro68-build/toolchain/bin/LaunchAPPL \
    -e tcp --tcp-address localhost \
    build/mac/test_mactcp.bin
```

### Step 3: MCP Integration
```python
# Test new QEMU machine in MCP
mcp__classic-mac-hardware__list_machines()
# Should show: qemu-753 (type: qemu)

mcp__classic-mac-hardware__test_connection(machine="qemu-753")
# Should start QEMU and verify LaunchAPPL

mcp__classic-mac-hardware__execute_binary(
    machine="qemu-753",
    platform="mactcp",
    binary_path="build/mac/test_mactcp.bin"
)
```

### Step 4: Full Test Workflow
```bash
# Start perf_partner on host
docker run -d --name perf-partner --network host peertalk-posix ./build/bin/perf_partner

# Run test via MCP
/run-test discovery qemu-753

# Verify logs appear in plan/performance/mactcp/qemu-753/
```

## Alternative: Simplified Disk-Only Mode

If networking proves too complex, fall back to Retro68's disk-based approach:
- Test apps write output to "out" file on shared disk
- MCP reads output after QEMU exits
- No MacTCP required, but no network testing either

This would still be useful for:
- Compilation verification
- Unit tests (non-networking)
- API testing

---

**Decision:** System 7.5.3 + MacTCP + LaunchAPPLServer, integrated as MCP machine type

**Status:** Planning complete - ready for implementation when needed
