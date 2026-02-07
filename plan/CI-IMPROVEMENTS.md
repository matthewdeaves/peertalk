# CI Improvements Backlog

Future enhancements to the CI/CD pipeline and metrics dashboard.

## Backlog

### Nightly Analysis Dashboard Integration
**Status:** TODO

Currently the nightly `cppcheck --force` job (`.github/workflows/nightly.yml`) outputs to:
- GitHub Actions job log
- Downloadable XML artifact

To add dashboard integration:
1. Call metrics extraction scripts from nightly job
2. Add `upload_to_pages.sh` step to push results
3. Create separate "nightly" section on dashboard to distinguish from per-commit metrics
4. Track historical trends for full cppcheck analysis

### Integration Tests in CI
**Status:** TODO

Currently NOT in CI:
- `test_integration_full` - 3-peer Docker test (requires docker-compose with 3 containers)
- `test_perf_benchmarks` - Performance benchmarks

**Adding `test_perf_benchmarks`:** Easy - single container, just add to test-local and extract output

**Adding `test_integration_full`:** Medium - needs docker-compose in GH Actions, 3 containers, more CI time

### Performance Metrics Tracking
**Status:** TODO

`test_perf_benchmarks.c` outputs metrics that could be tracked:
- Queue throughput: msgs/sec
- Push/pop latency: microseconds
- Priority overhead: %
- Coalescing efficiency: %
- Discovery encode/decode: ops/sec

`test_integration_full.c` tracks:
- peers_discovered, peers_connected
- messages_sent, messages_received
- broadcasts_sent

Could add commit-by-commit performance trends for POSIX to dashboard.
