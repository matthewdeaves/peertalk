# MacTCP Performance Logs

Historical test results from Classic Mac hardware. Used for:
- Performance regression tracking
- Optimization validation
- Historical comparisons for documentation/blog posts

## Folder Structure

```
mactcp/
  macse/              # Mac SE (68000, 4MB RAM, System 6.0.8)
  performa6200/       # Performa 6200 (PPC 603, 8MB RAM, System 7.5.5)
  PERFORMANCE-TESTING-REPORT.md  # Summary report
```

## Log Naming

Logs are saved by the POSIX `perf_partner` when Mac test apps stream results:
- Format: `{test_name}_{YYYYMMDD}_{HHMMSS}.log`
- Machine folder determined by peer IP address

## Test Types

| Test | Log Prefix | Description |
|------|------------|-------------|
| Throughput | `throughput_*` | Unidirectional streaming speed |
| Latency | `latency_*` | Round-trip echo timing |
| Stress | `stress_*` | Connect/disconnect cycles |
| Discovery | `discovery_*` | Peer discovery packet counts |

## Machine IPs

| Machine | IP | Characteristics |
|---------|-----|-----------------|
| Mac SE | 10.188.1.55 | 68000 @ 8MHz, 4MB, lowmem builds |
| Performa 6200 | 10.188.1.213 | PPC 603 @ 75MHz, 8MB, standard builds |

## Adding Results

1. Start `perf_partner` in Docker
2. Run test app on Mac hardware
3. Logs auto-stream to partner on test completion
4. Partner saves to appropriate machine folder
5. Commit logs to preserve history
