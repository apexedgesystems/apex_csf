# Executive Module

The executive is the central coordinator for Apex systems. POSIX-tier executives (`ApexExecutive` and derived) own the scheduler, filesystem, and component registry. MCU-tier executives (`McuExecutive`) own a static scheduler and tick source.

## Directory Tiers

| Tier     | Contents                                                             | Instantiable? |
| -------- | -------------------------------------------------------------------- | ------------- |
| `base/`  | Pure virtual `IExecutive` interface                                  | No            |
| `core/`  | `ExecutiveCore` shared base: identity constants, IExecutive contract | No            |
| `posix/` | `PosixExecutiveBase` + `ApexExecutive` (POSIX tier)                  | Yes           |
| `mcu/`   | `McuExecutive` (MCU tier)                                            | Yes           |

## Class Overview

| Class                | Tier  | Purpose                                                                      |
| -------------------- | ----- | ---------------------------------------------------------------------------- |
| `IExecutive`         | base  | Pure interface (run, shutdown, isShutdownRequested, cycleCount)              |
| `ExecutiveCore`      | core  | Shared base: COMPONENT_ID=0, COMPONENT_NAME="Executive", IExecutive contract |
| `PosixExecutiveBase` | posix | POSIX executive base (mixes SystemComponentBase + ExecutiveCore)             |
| `ApexExecutive`      | posix | Full POSIX executive with multi-threaded run loop, TPRM, logs, C2            |
| `McuExecutive`       | mcu   | Single-threaded MCU executive on `McuComponentBase` + ExecutiveCore          |

## Component Identity System

Every component the executive can register implements `IComponent` through `ComponentCore`. The shared concrete base owns the identity / lifecycle / registration state:

```cpp
class ComponentCore : public IComponent {
public:
  // Required overrides (pure virtual on ComponentCore)
  [[nodiscard]] virtual std::uint16_t componentId() const noexcept = 0;
  [[nodiscard]] virtual const char* componentName() const noexcept = 0;

  // Concrete: auto-assigned during registration
  [[nodiscard]] std::uint8_t instanceIndex() const noexcept;
  [[nodiscard]] std::uint32_t fullUid() const noexcept;  // (componentId << 8) | instanceIndex
};
```

Both `SystemComponentBase` (POSIX tier) and `McuComponentBase` (MCU tier) inherit `ComponentCore`. `ComponentRegistry` accepts `ComponentCore*`, so either tier can register through the same code path.

### fullUid Composition

```
fullUid = (componentId << 8) | instanceIndex

Example:
  componentId = 102, instanceIndex = 1
  fullUid = (102 << 8) | 1 = 0x6601
```

### Multi-Instance Support

Components with the same `componentId` AND `componentName` can have multiple instances:

```cpp
PolynomialModel poly1, poly2;
exec.registerComponent(&poly1);  // instanceIndex=0, fullUid=0x6600
exec.registerComponent(&poly2);  // instanceIndex=1, fullUid=0x6601
```

### Collision Detection

| Same componentId | Same componentName | Result                    |
| ---------------- | ------------------ | ------------------------- |
| Yes              | Yes                | OK (multi-instance)       |
| Yes              | No                 | ERROR_COMPONENT_COLLISION |

## Component ID Registry

| Range | Purpose                                       |
| ----- | --------------------------------------------- |
| 0     | Executive (reserved)                          |
| 1-100 | System components (Scheduler=1, FileSystem=2) |
| 101+  | Simulation models                             |

### Assigned IDs

| ID  | Component          |
| --- | ------------------ |
| 0   | Executive          |
| 1   | Scheduler          |
| 2   | FileSystem         |
| 101 | SequencedDemoModel |
| 102 | PolynomialModel    |
| 103 | GravityDemoModel   |

## TPRM Loading

The executive loads a packed tprm file containing configuration for all components:

```
master.tprm
    |
    +-> UID 0: Executive tunables (48 bytes)
    +-> UID 1: Scheduler task config (variable)
    \-> UID 101+: Model tunables (variable)
```

Individual tprms are extracted to `.apex_fs/tprm/` during init.

## Ingest Policy

Every component reports its boot ingest as one of four states — NONE
(no TPRM surface), LOADED, DEFAULTS (no file, built-in defaults),
REJECTED (file present but refused) — and the executive judges them
together at a post-registration barrier. `--ingest-policy
strict|lenient` selects the posture; **STRICT is the default**:

| State                               | STRICT | LENIENT    |
| ----------------------------------- | ------ | ---------- |
| REJECTED                            | fatal  | fatal      |
| DEFAULTS, params not optional       | fatal  | warn + run |
| DEFAULTS, `paramsOptional()`        | run    | run        |
| NONE with registered TUNABLE_PARAMs | fatal  | warn + run |

A rejected payload is fatal under every policy: a present-but-refused
file is never intentional. Components whose defaults are a designed
configuration override `paramsOptional()`. The executive's own
payload follows the same chain: a rejected executive TPRM joins the
barrier (compiled defaults never RUN -- the fallback bank heals it,
or the SAFE hold keeps the vehicle reachable on defaults that stay
inert because nothing dispatches). A master packed without an
executive entry remains the designed explicitly-no-config state.

## Boot Recovery and SAFE

A failed ingest triggers one bounded A/B recovery: if the other bank
holds staged payloads, the executive flips `active_bank` and re-execs;
the fallback boot skips master extraction (the master is what failed)
and announces RUNNING ON FALLBACK BANK at ERROR level.

When no bank passes, the executive enters **SAFE/HOLD**. SAFE's
invariant, application-wide: **the command path survives
configuration failure** — any bad load short of a crashed binary
stays reachable and repairable over the wire. Three SAFE levels share
that invariant, ordered by how much of the vehicle still moves:

| Level         | What runs                                           | Status      |
| ------------- | --------------------------------------------------- | ----------- |
| SAFE/HOLD     | nothing cycles; wire alive                          | implemented |
| SAFE/IDLE     | core + support tier tasks (watchable); no app tasks | future      |
| SAFE/DEGRADED | all but the failed component's dependency cone      | future      |

HOLD is the only honest answer for a misconfigured core component or
an undeclared dependency graph; IDLE's skip set falls out of the
component taxonomy's tier axis; DEGRADED needs declared dependencies
(see the SAFE-orchestration backlog ticket). HOLD is parked-at-boot:

| Thread        | In SAFE hold                          |
| ------------- | ------------------------------------- |
| Clock         | parked (no cycles advance)            |
| TaskExecution | waiting (no task ever dispatches)     |
| ExternalIO    | serving (full command/telemetry path) |
| Watchdog      | running                               |

RESUME and WAKE are refused with INGEST_HELD; the repair loop is the
standard surface — READBACK_TPRM to inspect the staged banks, file
upload to place a corrected set, VERIFY_TPRM to check it, and
RELOAD_EXECUTIVE to reboot onto the fix. `--shutdown-after` still
applies, so automated runs exit rather than hang.

## Library Reload

RELOAD*LIBRARY replaces a locked component with one loaded from
`<name>*<instance>.so` in the inactive library bank. It runs in three
steps, on the threads that own what each step touches:

| Step    | Thread                                  | Work                                                                                                                       |
| ------- | --------------------------------------- | -------------------------------------------------------------------------------------------------------------------------- |
| Prepare | ExternalIO                              | load the library, create the instance, give it its log, load its parameters, initialize it, archive the old instance's log |
| Publish | TaskExecution                           | bind the scheduler's entries, the registry's entries and the component list to the new instance                            |
| Reclaim | ExternalIO, at the top of its next loop | destroy the plugin the swap replaced and close its library                                                                 |

Publication happens only while the scheduler reports every dispatched
task settled (no task queued or running, no worker still filing a task's
completion) and ExternalIO is waiting for it. TaskExecution is the thread
that drains commands to components, dispatches tasks and runs the action
phase, so at that point nothing resolves the component while its
bindings change. The new instance is dispatched from the next cycle; the
old one stays locked.

- **What is bound.** In the scheduler, each entry's task and, for a
  sequenced task, its group's counter; the scheduler keeps its own copy
  of component names. In the registry, the component entry, each
  registered data block and each registered task. INSPECT, action data
  access, telemetry and the shared-memory bridge read the new instance's
  blocks from the next lookup on. Data consumers resolve per use: a
  pointer kept from an earlier lookup names the replaced instance.
- **Compatibility.** The replacement must provide every registered data
  block (same category, name and size) and every scheduled task,
  sequenced in a group at the same phase. Otherwise the reply is
  `REGISTRY_FAILED` or `TASK_MISMATCH` and nothing was bound. Data
  blocks beyond the registered ones are not added: the registry is frozen.
- **Wait.** ExternalIO hands the swap to TaskExecution and blocks until
  it is published or two seconds pass (four clock periods when that is
  longer). Command and telemetry I/O wait with it. TaskExecution takes
  the swap after a cycle whose tasks finished within their frame,
  between cycles, or while paused.
- **Refusal.** If the tasks do not settle within the wait, the reply is
  `NOT_QUIESCENT`. A task that overruns every frame keeps a reload out
  until it returns. After any refusal the new instance is destroyed and
  the old one is unlocked and unchanged.
- **From an action.** A RELOAD_LIBRARY an action sends reaches the
  handler on TaskExecution. It is recorded (`RELOAD_LIBRARY_DEFERRED` in
  the swap log) and run by ExternalIO at the top of its next loop, so no
  library is loaded and no component initialized inside a cycle. The
  action's command returns once recorded; the outcome is in the system
  log and the swap log. One request can wait at a time; a second is
  refused with `NOT_SWAPPABLE`.
- **Cost when no reload is in hand.** TaskExecution loads one flag on
  each wake (an exchange follows only when it is set) and one after each
  cycle whose tasks finished; its tick and pause wait predicates load
  the first flag when evaluated. ExternalIO checks an empty list and
  loads one word per loop.

The swap log times the three steps apart: `RELOAD_LIBRARY_PUBLISHED`
carries the wait for publication (`wait_us`), `RELOAD_LIBRARY_OK` the
whole command from its start (`total_us`), and
`RELOAD_LIBRARY_RECLAIMED` the destruction of the replaced plugin (`us`).

A component lock stops new dispatches of the component's tasks. It does
not wait for a task already running; the reload does.

At shutdown the scheduler is stopped in the stage that drains tasks,
once they have drained, while the components and loaded plugins are
alive.

## Usage

```bash
./ApexDemo \
  --config build/hosted-x86_64-debug/demos/apex_ops_demo/exec/tprm/master.tprm \
  --archive-path /path/to/output \
  --shutdown-after 5
```

## Init Sequence

The executive init phase performs several steps after component registration
and TPRM loading:

- **Build identity**: The `=== System Configuration ===` block opens with
  `Build: <summary>`, the source state and configuration the binaries came
  from (`git describe`, branch, tree state, build type, compiler, target;
  see `src/utilities/build_info/README.md`), so a run log names its build.

- **Core component queue allocation**: Allocates command queues for all core
  components (scheduler, filesystem, registry, action) so they can receive
  internal bus commands from support components during init.

- **Catalog scan**: Calls `scanCatalog()` on the ActionComponent to populate
  the sequence catalog with all RTS and ATS files found in the filesystem.

- **onBusReady lifecycle**: After all components are configured and the
  internal bus is wired, the executive calls `onBusReady()` on every
  registered component. Components override this hook to issue startup
  commands to other components (e.g., a support component loading a sequence
  into the action engine). All queued commands are drained before runtime
  starts.

## Clock Cadence

The clock thread sleeps to an absolute deadline grid and signals the
task-execution thread on every wake. Tick `n` is due at the anchor plus
`n / Hz` whole seconds plus the `(n mod Hz)`-th share of a second
(`ClockGrid.hpp`), so every rate in [1, 1000] Hz is honoured: a rate
that does not divide a second into whole nanoseconds (600 Hz is
1,666,666.67 ns) keeps every second boundary exact and is never more
than a nanosecond off inside one. The wait is one absolute-deadline
sleep on the monotonic clock (`clock_nanosleep` with `TIMER_ABSTIME`),
and the clock thread runs with one nanosecond of timer slack, so an
ordinary-policy boot is not subject to the kernel's default 50 us of
wake coalescing. The 1000 Hz ceiling is a stated limit of the POSIX
executive, not a property of the arithmetic.

Each wake is recorded against the deadline it slept to, with no
allocation on the tick path, and the distribution is summarised when
the clock stops:

```text
Tick lateness: 3000 wakes, p50 <= 61 us, p99 <= 143 us, max 7212.3 us, mean 58.4 us, 1 ms or later: 2, early: 0
```

Read it as "that share of wakes were no later than this": the
percentiles are bucket upper edges at one-microsecond resolution. The
figures are the host's timer resolution, timer slack and scheduler
latency as the clock experiences them, so they are the first thing to
look at when a run reports frame overruns or lag. In-process callers get
the same record from `clockLateness()` once `run()` has returned.

`--clock-hz N` overrides the configured fundamental for a run, within the
validator's [1, 1000] Hz; the cadence benchmark uses it to boot at the
rate under test.

## Testing

| Dir           | Contents                                                         | In CI suite? |
| ------------- | ---------------------------------------------------------------- | ------------ |
| `posix/utst/` | Struct/config unit tests; RELOAD_LIBRARY on a live executive     | Yes          |
| `base/utst/`  | IExecutive contract tests                                        | Yes          |
| `core/utst/`  | ExecutiveCore identity/contract tests                            | Yes          |
| `mcu/utst/`   | MCU executive unit tests                                         | Yes          |
| `posix/dtst/` | Full-boot timing-integrity component tests (clock grid repayment | No (manual)  |
|               | across a real SIGSTOP stall, backlog drain to parity, counted    |              |
|               | >1 s resync, pause neither owed nor counted)                     |              |
| `posix/ptst/` | Delivered-cadence benchmark (period fidelity, jitter tail,       | No (manual)  |
|               | cumulative drift vs the deadline grid, wake lateness) at the     |              |
|               | rate `CADENCE_HZ` selects                                        |              |

The dtst and ptst tiers boot a real `ApexExecutive` (config-less CLI
boot, cycle-target shutdown) and assert wall-clock spans, so they run
manually on an otherwise-idle host.

## RT Safety

| Function              | RT-Safe | Notes                       |
| --------------------- | ------- | --------------------------- |
| `init()`              | No      | Allocates, performs I/O     |
| `run()`               | Yes     | Main loop is RT-safe        |
| `shutdown()`          | No      | Joins threads, flushes logs |
| `registerComponent()` | No      | Modifies registry           |

## See Also

- `demos/apex_ops_demo/README.md` - Reference demo application
