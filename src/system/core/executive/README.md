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

## Testing

| Dir           | Contents                                                          | In CI suite? |
| ------------- | ----------------------------------------------------------------- | ------------ |
| `posix/utst/` | Struct/config unit tests (status, RT mode, data, shutdown)        | Yes          |
| `base/utst/`  | IExecutive contract tests                                         | Yes          |
| `core/utst/`  | ExecutiveCore identity/contract tests                             | Yes          |
| `mcu/utst/`   | MCU executive unit tests                                          | Yes          |
| `posix/dtst/` | Full-boot timing-integrity component tests (clock grid repayment, | No (manual)  |
|               | backlog drain to parity, counted >1 s resync)                     |              |
| `posix/ptst/` | Delivered-cadence benchmark (period fidelity, jitter tail,        | No (manual)  |
|               | cumulative drift vs the deadline grid)                            |              |

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
