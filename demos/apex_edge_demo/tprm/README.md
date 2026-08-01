# Edge Compute Demo - TPRM Configuration

## Layout

```
tprm/
  tprm.manifest            Packing recipe (see cmake/apex/Tprm.cmake)
  toml/
    executive.toml         Executive config (native)
    scheduler.toml         Native scheduler (1 pool, 8 workers)
    action.toml            Action component (empty, no watchpoints)
    batch_stats.toml       BatchStatsModel workload config
    conv_filter.toml       ConvFilterModel workload config
    fft_analyzer.toml      FFTAnalyzerModel workload config
    stream_compact.toml    StreamCompactModel workload config
    system_monitor.toml    SystemMonitor thresholds (native)
    thor/
      executive.toml       Thor executive config
      scheduler.toml       Thor scheduler (1 pool, 14 workers)
      system_monitor.toml  Thor SystemMonitor thresholds
```

`tprm.manifest` is the packing recipe. The shared algorithm chain lives in
`[components algorithms]`; the `master.tprm` block adds the native
executive/scheduler/system_monitor TOMLs and the `master_thor.tprm` block adds
the `toml/thor/` timing trio. The CMake target `apex_tprm_ApexEdgeDemo` (part
of the default build) compiles each TOML with cfg2bin and packs both masters
into `build/<preset>/demos/apex_edge_demo/exec/tprm/`:

| Archive            | Timing trio  | Use                              |
| ------------------ | ------------ | -------------------------------- |
| `master.tprm`      | `toml/`      | Native development (dev PC)      |
| `master_thor.tprm` | `toml/thor/` | Thor deployment (`make release`) |

Composition is set union with no shadowing: a fullUid arriving twice in one
master is a configure error.

## Component fullUid Map

| fullUid  | Component          | TOML Source              |
| -------- | ------------------ | ------------------------ |
| 0x000000 | Executive          | toml/executive.toml      |
| 0x000100 | Scheduler          | toml/scheduler.toml      |
| 0x000500 | Action             | toml/action.toml         |
| 0x008200 | ConvFilterModel    | toml/conv_filter.toml    |
| 0x008300 | FFTAnalyzerModel   | toml/fft_analyzer.toml   |
| 0x008400 | BatchStatsModel    | toml/batch_stats.toml    |
| 0x008500 | StreamCompactModel | toml/stream_compact.toml |
| 0x00C800 | SystemMonitor      | toml/system_monitor.toml |

`master_thor.tprm` packs the `toml/thor/` variant of the executive, scheduler,
and system monitor entries.

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

Edit the TOML source, then rebuild: the `apex_tprm_ApexEdgeDemo` target
recompiles the changed TOML and repacks both masters. The manifest drives
packing end to end; there is no hand-run cfg2bin/tprm_pack step.
