# Classic Macintosh TCP/IP Reference

> Compiled from vintagemacworld.com (archived May 2006) by Phil Beesley

This document covers TCP/IP networking on classic Macintosh systems from System 6 through Mac OS 9.x. It does not apply to Mac OS X.

---

## Quick Reference: Which Stack to Use?

| Mac Type | System Version | Networking Stack |
|----------|----------------|------------------|
| 68000/68020 (SE, Plus, Classic I, LC, Mac II) | System 6 - 7.5.5 | MacTCP only |
| 68030 with <8MB RAM | System 6 - 7.6.1 | MacTCP recommended |
| 68030 with 8MB+ RAM | System 7.1 - 7.6.1 | Either (OT optional) |
| 68040 | System 7.1+ | Open Transport recommended |
| PowerPC (NuBus) | System 7.1+ | Open Transport recommended |
| PowerPC (PCI bus) | System 7.5.2+ | Open Transport required |
| Any Mac | Mac OS 8.0 - 9.2.2 | Open Transport built-in |

---

## System 6 Networking

### Requirements
- **Minimum:** System 6.0.7 or 6.0.8 recommended
- **Stack:** MacTCP only (Open Transport not supported)
- **Note:** MacTCP does not work with systems older than System 6

### Ethernet Driver Installation

System 6 does not include ethernet drivers. Install drivers BEFORE MacTCP.

**Network Software Installer 1.4.5** (recommended):
```
ftp://download.info.apple.com/Apple_Support_Area/Apple_Software_Updates/
  English-North_American/Macintosh/Networking-Communications/
  Network_Software_Installer/NSI_ZM-1.4.5.sea.bin
```

**Network Software Installer 1.4.4** (alternate):
```
ftp://download.info.apple.com/Apple_Support_Area/Apple_Software_Updates/
  English-North_American/Macintosh/Networking-Communications/
  Network_Software_Installer/NSI_1.4.4.sea.bin
```

**Third-party drivers:** Mac Driver Museum (see Resources section)

---

## System 7.0 - 7.6.1 Networking

### Stack Options

- **System 7.0 - 7.0.1:** MacTCP only (OT not reliably supported)
- **System 7.1 - 7.5.x:** MacTCP or Open Transport 1.1.1+
- **System 7.6 - 7.6.1:** Open Transport included, MacTCP optional
- **PCI Macs:** Open Transport required (no MacTCP option)

### Ethernet Driver Installation

**System 7.0 - 7.1:** Install Network Software Installer 1.5.1:
```
ftp://download.info.apple.com/Apple_Support_Area/Apple_Software_Updates/
  English-North_American/Macintosh/Networking-Communications/
  Network_Software_Installer/ZM-NSI_1.5.1.sea.bin
```

**System 7.5+:** Includes drivers for Apple cards and some third-party cards (Asante, Sonic-compatible). Manufacturer drivers recommended for full feature support.

---

## Mac OS 8.0 - 9.2.2 Networking

- Open Transport is built-in and mandatory
- More robust than earlier OT versions
- Check for OT updates matching your Mac OS version
- Some "Road Apple" models (5200 series) may need ROM upgrade for Mac OS 8

---

## MacTCP Details

### Overview
- Only option for 68000/68020 Macs
- Works with Mac Plus and later (not 128K or 512K; may work with 512Ke)
- Faster and less memory-hungry than Open Transport
- Cannot use DHCP (only RARP, BootP, or manual configuration)
- Automatic addressing works with MacPPP dial-up

### MacTCP 2.0.6 (Official Final Release)

**Download:**
```
ftp://ftp.apple.com/devworld/Development_Kits/MacTCP.sit.hqx
```

### MacTCP 2.1 (Unofficial Patch by Glenn Anderson)

Fixes several bugs in 2.0.6:
- Land attack vulnerability
- Classless subnet mask configuration UI
- TCP FIN handling (connections stuck in CLOSE_WAIT)
- ICMP TTL reset to 255 (fixes traceroute)
- BlockMove changed to BlockMoveData (performance)
- Removed debugger breaks

**Requires:** Clean, unmodified MacTCP 2.0.6 to patch

**Info:** http://www.mactcp.org.nz/mactcp.html

### Configuring MacTCP 2.0.6 (Manual/Static IP)

1. Open MacTCP control panel
2. Select **Ethernet** icon (not EtherTalk - that's for MacIP)
3. Press **More...** button
4. **Obtain Address:** Select **Manually**
5. **Routing Information:** Enter Gateway Address (your router's IP)
6. **IP Address:** Set Class to **C**, Subnet Mask to **255.255.255.0**
7. **Domain Name Server:**
   - Domain field: Enter a single period (`.`)
   - Add each DNS server IP address
   - Select one as **Default**
8. Press **OK**
9. Enter Mac's IP address in main window
10. Close control panel and **restart**

### Troubleshooting MacTCP

When reinstalling or reconfiguring:
1. Delete `MacTCP Prep` from System Folder
2. Delete `MacTCP DNR` from System Folder
3. Reinstall/reconfigure MacTCP
4. Restart

---

## Open Transport Details

### Overview
- Requires 68030 or later with System 7.1+
- Realistically needs fast 68030 and 8MB+ RAM
- Mandatory on PCI Macs
- Includes both TCP/IP and AppleTalk components
- Will not install on accelerated 68000/68020 (installer checks for built-in 68030)

### Version History

| Version | Notes |
|---------|-------|
| Pre-1.1.1 | Unreliable, especially DHCP. Avoid. |
| 1.1.1 | First reliable version |
| 1.1.2 | Recommended for System 7.1 - 7.6.1 |
| 1.2+ | Bundled with Mac OS 7.6.1+ (cannot install on older systems) |

**Important:** OT 1.1.2 is an *updater* - install OT 1.1.1 first, then apply 1.1.2.

### Checking for Open Transport

| You Have... | You're Running... |
|-------------|-------------------|
| "Network" control panel | Classic Networking (MacTCP) |
| "AppleTalk" + "TCP/IP" control panels | Open Transport |
| "Network Software Selector" utility | Mac supports both; use utility to switch |

### Checking OT Version

1. Open **TCP/IP** control panel
2. **File** menu → **Get Info**
3. Displays OT version and ethernet MAC address

### Configuring TCP/IP (Open Transport)

For DHCP issues on older OT versions, configure manually:

1. **Edit** menu → **User Mode** → **Advanced**
2. Manually enter Gateway and DNS addresses
3. These override DHCP-provided values

### TCP/IP "Always On" Mode

By default, TCP/IP loads only when an app needs it. To keep it loaded:

1. **Edit** menu → **User Mode** → **Advanced**
2. Press **Options** button
3. Uncheck **"Load only when needed"**

### Compatibility Note

99% of MacTCP applications work with Open Transport. Known exception: White Pine eXodus X11 server.

---

## Model-Specific Recommendations

### Must Use MacTCP
- Mac Plus, SE, Classic I
- Mac II, IIx (original)
- LC, LCII
- Any 68000 or 68020 Mac

### MacTCP Recommended (OT marginal)
- LCII, Classic II - OT not recommended
- SE/30, IIx, IIcx, IIvi, IIvx - OT works better with accelerators

### Either Stack Works Well
- IIci, LCIII, IIfx - Oldest models where difference is negligible

### Open Transport Recommended
- 68040 Macs (Quadra series)
- All PowerMacs

### Special Cases
- 5200, 5300, 6200, 6300 families may not run later OT versions
- See OT 1.1.2 ReadMe for details

---

## Dial-Up PPP Software

### MacPPP 2.0.1
- Best choice for MacTCP users
- Works with System 6, 7.0, 7.1
- Reliable automatic IP addressing with MacTCP

**Download (bundled with MacTCP 2.0.6):**
```
http://www.jagshouse.com/software/MacPPP.sit.hqx
```

### FreePPP
- Based on MacPPP
- Works with both MacTCP and Open Transport

### Open Transport PPP (OT/PPP)
- Works with Open Transport 1.1.1+ only (not MacTCP)
- Best PPP choice for OT users
- Included with Mac OS 7.6+

**Download (for pre-7.6 systems):**
```
ftp://download.info.apple.com/Apple_Support_Area/Apple_Software_Updates/
  English-North_American/Macintosh/Networking-Communications/
  Open_Transport/OT_PPP_1.0-Net_Install.sea.bin
```

---

## Ethernet Cards and Drivers

### General Guidance

1. **Install drivers BEFORE MacTCP/OT**
2. Use manufacturer drivers when available (enables full features)
3. Test with AppleTalk first - it should work automatically
4. AppleTalk success confirms card/driver work; then troubleshoot TCP/IP

### Testing Ethernet Card

Connect to an AppleShare server (System 7 or Mac OS 9 with File Sharing). **Do not** use Mac OS X file sharing for testing - it uses AppleShare-IP (TCP/IP based) which won't confirm basic connectivity.

---

## TCP/IP Troubleshooting

### Ping Test Sequence

Run from the Mac with questionable connectivity:

1. **Ping local gateway** (your router)
   - Failure: IP conflict or MacTCP/OT misconfigured

2. **Ping ISP gateway** (numeric IP, not DNS name)
   - Note: Some NAT routers don't respond to ping
   - Failure: Routing or configuration issue

3. **Ping ISP server by DNS name** (e.g., www.my-isp.com)
   - Failure: DNS resolution issue; manually configure DNS servers

4. **Ping external site** (e.g., www.sun.com)
   - Success: Full internet connectivity confirmed

### Important: TCP/IP Loading Behavior

Classic Mac OS doesn't load TCP/IP at startup by default. The Mac won't respond to pings until you run a TCP/IP application.

**Solutions:**
- Run a TCP/IP app (ping utility, browser) on the Mac being tested
- Or enable "always on" mode in OT (see configuration section)

---

## Utility Software

### Ping Tools

**MacTCP Ping 2.0.2** (Apple official, works with MacTCP and OT):
```
ftp://download.info.apple.com/Apple_Support_Area/Apple_Software_Updates/
  English-North_American/Macintosh/Misc/MacTCP_Ping_2.0.2.sea.bin
```

### Network Toolkits

| Tool | Platform | Features |
|------|----------|----------|
| Network Toolbox (Black Cat Systems) | PowerPC | Comprehensive network tools |
| IPNetMonitor (Sustworks) | 68K + PPC (System 7.5.3+) | Full diagnostic suite |
| WhatRoute | 68K + PPC | Ping, traceroute, Telnet server |

---

## Resources

### Driver Downloads
- **Mac Driver Museum:** http://host59.ipowerweb.com/~macdrive/

### Software Collections
- **Mac Orchard:** http://www.macorchard.com/
- **Low-End Mac FAQ:** http://macfaq.org/

### Localized Software
Non-US English system software:
```
ftp://download.info.apple.com/Apple_Support_Area/Apple_Software_Updates/MultiCountry/
```

### SSH Clients
- **OpenSSH Mac clients:** http://www.openssh.com/macos.html

### Documentation
- **OT 1.1.2 ReadMe:** http://www.gla.ac.uk/~gwm1h/ot/ot1.1.2/OT_1.1.2_ReadMe-Part1.html

---

## Quick Checklist: New TCP/IP Setup

1. [ ] Identify Mac model and determine MacTCP vs OT
2. [ ] Install appropriate System version
3. [ ] Install ethernet card drivers (if needed)
4. [ ] Install networking stack (MacTCP or OT)
5. [ ] Configure IP settings (manual for MacTCP, DHCP or manual for OT)
6. [ ] Test with ping utility
7. [ ] If using dial-up, install appropriate PPP software

---

*Original source: vintagemacworld.com/mactcpip.html (Phil Beesley)*
*Archived: May 17, 2006 via Wayback Machine*
