# PeerTalk Performance Testing

This folder contains performance test results and analysis for the PeerTalk SDK across different networking platforms.

## Folder Structure

```
performance/
├── README.md                    # This file
├── mactcp/                      # MacTCP networking stack
│   ├── PERFORMANCE-TESTING-REPORT.md   # Comprehensive analysis
│   └── logs/
│       ├── performa6200/        # Performa 6200 (75MHz PPC, 8MB)
│       │   ├── PT_Throughput*   # 20 throughput test runs
│       │   ├── PT_Latency*      # 8 latency measurement runs
│       │   ├── PT_Stress*       # 7 stress test cycles
│       │   └── PT_Discovery     # Discovery validation
│       └── macse/               # Mac SE (8MHz 68000, 4MB)
│           └── macse_PT_Throughput*  # Low-memory test runs
├── opentransport/               # (Future) Open Transport stack
│   └── ...
└── appletalk/                   # (Future) AppleTalk/ADSP
    └── ...
```

## Test Hardware

| Machine | CPU | RAM | Network Stack | Status |
|---------|-----|-----|---------------|--------|
| Performa 6200 | 75 MHz 603e PPC | 8 MB | MacTCP 2.0.6 | ✓ Complete |
| Mac SE | 8 MHz 68000 | 4 MB | MacTCP | ✓ Complete |
| Power Mac | TBD | TBD | Open Transport | Pending |

## Key Results Summary

### MacTCP (Performa 6200)

| Message Size | Max SEND | Max RECV | Balance |
|-------------|----------|----------|---------|
| 256 bytes | 11 KB/s | 11 KB/s | ✓ 100% |
| 512 bytes | 24 KB/s | 24 KB/s | ✓ 100% |
| 1024 bytes | 49 KB/s | 49 KB/s | ✓ 100% |
| 2048 bytes | 112 KB/s | 32 KB/s | 29% |

### MacTCP (Mac SE)

| Message Size | Max SEND | Max RECV | Notes |
|-------------|----------|----------|-------|
| 256 bytes | 3 KB/s | 3 KB/s | CPU-limited |
| 512 bytes | 5 KB/s | 5 KB/s | CPU-limited |
| 1024 bytes | 8 KB/s | 8 KB/s | CPU-limited |

## Log File Naming Convention

| Pattern | Description |
|---------|-------------|
| `PT_Throughput` | Baseline throughput test |
| `PT_Throughput_async` | After async send implementation |
| `PT_Throughput_v2_final` | Original baseline measurement |
| `PT_Latency_final` | Complete latency measurement |
| `PT_Stress_v*` | Stress test iterations |
| `macse_*` | Mac SE specific tests |

## Adding New Platform Results

When testing a new platform (e.g., Open Transport):

1. Create folder: `performance/opentransport/`
2. Add logs: `performance/opentransport/logs/<machine>/`
3. Create report: `performance/opentransport/PERFORMANCE-TESTING-REPORT.md`
4. Update this README with results summary

## Related Documents

- `plan/REFACTOR-performance-*.md` - Performance optimization planning
- `plan/CSEND-LESSONS.md` - Lessons learned from async send
- `.claude/rules/mactcp.md` - MacTCP API rules and gotchas
