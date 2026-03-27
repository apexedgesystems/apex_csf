# Edge Compute Demo - TPRM Configuration

## Layout

```
tprm/
  master.tprm              Packed archive for native development (dev PC)
  master_thor.tprm         Packed archive for NVIDIA Thor deployment
  executive.tprm           Executive config (shared)
  scheduler.tprm           Native scheduler (1 pool, 8 workers)
  action.tprm              Action component (empty, no watchpoints)
  batch_stats.tprm         BatchStatsModel workload config
  conv_filter.tprm         ConvFilterModel workload config
  fft_analyzer.tprm        FFTAnalyzerModel workload config
  stream_compact.tprm      StreamCompactModel workload config
  system_monitor.tprm      SystemMonitor thresholds (native)
  thor/
    executive.tprm         Thor executive config
    scheduler.tprm         Thor scheduler (1 pool, 14 workers)
    system_monitor.tprm    Thor SystemMonitor thresholds
  toml/
    *.toml                 TOML source files (native)
    thor/*.toml            TOML source files (Thor overrides)
```

## Component fullUid Map

| fullUid  | Component          | TPRM File           |
| -------- | ------------------ | ------------------- |
| 0x000000 | Executive          | executive.tprm      |
| 0x000100 | Scheduler          | scheduler.tprm      |
| 0x000500 | Action             | action.tprm         |
| 0x008200 | ConvFilterModel    | conv_filter.tprm    |
| 0x008300 | FFTAnalyzerModel   | fft_analyzer.tprm   |
| 0x008400 | BatchStatsModel    | batch_stats.tprm    |
| 0x008500 | StreamCompactModel | stream_compact.tprm |
| 0x00C800 | SystemMonitor      | system_monitor.tprm |

## Platform Differences

Model TPRMs (workload sizes, frequencies) are shared across platforms. Only
executive, scheduler, and system monitor differ per target:

| Parameter                  | Native    | Thor              |
| -------------------------- | --------- | ----------------- |
| Scheduler pools            | 1         | 1                 |
| Workers per pool           | 8         | 14                |
| Monitor cores              | [0,1,2,3] | [0,1,2,3,4,5,6,7] |
| Monitor GPU temp warn/crit | 75/85 C   | 80/90 C           |

## Regeneration

After editing any TOML source file, regenerate the binary TPRMs and repack:

```bash
# Native master.tprm
./build/native-linux-debug/bin/tools/rust/cfg2bin \
  --batch apps/apex_edge_demo/tprm/toml \
  --output apps/apex_edge_demo/tprm

./build/native-linux-debug/bin/tools/rust/tprm_pack pack \
  -e 0x000000:apps/apex_edge_demo/tprm/executive.tprm \
  -e 0x000100:apps/apex_edge_demo/tprm/scheduler.tprm \
  -e 0x000500:apps/apex_edge_demo/tprm/action.tprm \
  -e 0x008200:apps/apex_edge_demo/tprm/conv_filter.tprm \
  -e 0x008300:apps/apex_edge_demo/tprm/fft_analyzer.tprm \
  -e 0x008400:apps/apex_edge_demo/tprm/batch_stats.tprm \
  -e 0x008500:apps/apex_edge_demo/tprm/stream_compact.tprm \
  -e 0x00C800:apps/apex_edge_demo/tprm/system_monitor.tprm \
  -o apps/apex_edge_demo/tprm/master.tprm

# Thor master_thor.tprm
./build/native-linux-debug/bin/tools/rust/cfg2bin \
  --batch apps/apex_edge_demo/tprm/toml/thor \
  --output apps/apex_edge_demo/tprm/thor

./build/native-linux-debug/bin/tools/rust/tprm_pack pack \
  -e 0x000000:apps/apex_edge_demo/tprm/thor/executive.tprm \
  -e 0x000100:apps/apex_edge_demo/tprm/thor/scheduler.tprm \
  -e 0x000500:apps/apex_edge_demo/tprm/action.tprm \
  -e 0x008200:apps/apex_edge_demo/tprm/conv_filter.tprm \
  -e 0x008300:apps/apex_edge_demo/tprm/fft_analyzer.tprm \
  -e 0x008400:apps/apex_edge_demo/tprm/batch_stats.tprm \
  -e 0x008500:apps/apex_edge_demo/tprm/stream_compact.tprm \
  -e 0x00C800:apps/apex_edge_demo/tprm/thor/system_monitor.tprm \
  -o apps/apex_edge_demo/tprm/master_thor.tprm
```
