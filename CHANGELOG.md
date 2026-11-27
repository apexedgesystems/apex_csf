# Changelog

All notable changes to Apex CSF are documented in this file. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
While the major version is 0, minor releases may contain breaking changes;
any such change is called out explicitly in its entry.

## [Unreleased]

### Added

- A bundle's `TOOLS` takes Rust, shell and Python tools as well as C++
  targets, staged to run from wherever the package is copied; a tool that
  cannot run on the target fails the build
  ([#192](https://github.com/apexedgesystems/apex_csf/pull/192)).
- Executives log their build identity (`git describe`, branch, tree state,
  build type, compiler, target) as the first System Configuration line.
  ([#172](https://github.com/apexedgesystems/apex_csf/pull/172))
- Firmware images carry the build identity: the library builds for the MCU
  platforms, and each firmware writes `Build: <identity>` as its first UART
  line. ([#181](https://github.com/apexedgesystems/apex_csf/pull/181))
- Cached scalar Legendre evaluation from precomputed tables
  (`ScalarTables`) for repeated calls up to a fixed degree.
  ([#167](https://github.com/apexedgesystems/apex_csf/pull/167))
- `make check-data-db`: every registered data manifest generates its
  dictionary clean; CI runs it whenever a manifest, header, registration or
  generator changes. An explicit `MANIFEST` path that does not exist now
  fails configure ([#166](https://github.com/apexedgesystems/apex_csf/pull/166)).
- Executive clock wake-lateness record: every clock wake is measured
  against its deadline and summarised (p50, p99, max) when the clock
  stops; `--clock-hz` overrides the fundamental frequency for a run.
- Flight-dynamics simulation domains: aerodynamics, rigid-body dynamics,
  propulsion, guidance/navigation/control (organized by vehicle class), and
  sensor measurement models.
- Multi-fidelity circuit simulation: MNA solvers (sparse, dense, batched
  CUDA), transient analysis with companion models, and device physics from
  R/L/C through MOSFET and BJT models.
- Environment models: atmosphere, terrain elevation, and celestial-body
  components alongside spherical-harmonic gravity, unified behind a
  `(Body, fidelity)` factory.
- Six demo applications: operations telemetry (Zenith), action engine,
  circuit selection, CPU simulation at three fidelity levels, time server,
  and a lidar visualization producer.
- Vehicle demo family for an external visualizer: a rover on tiled terrain
  (ROVR/2) and a transport aircraft in a standard atmosphere (ACFT/2) with
  one-command dynamic-mode set-pieces (short period, Dutch roll, spiral,
  phugoid), a 20 Hz mode trace, and a measured recovery boundary that
  restores the autopilot when a demonstration leaves the flight envelope.
- rover_mcu demo: rover drive control on the host or a NUCLEO-F767ZI,
  with safety halts, mission upload and pushed telemetry
  ([#151](https://github.com/apexedgesystems/apex_csf/pull/151)).
- Nightly assurance pipeline: warm firmware builds for every MCU target,
  sanitizer suites, cppcheck, dependency and container scans, and
  per-language coverage floors; CodeQL analysis on the default branch.
- Data-driven platform registry: compose services, CI matrices, and image
  lists are generated from one table and drift-gated in CI.
- Hermetic dependency resolution: Rust crates and Python wheels are baked
  into the build images and resolved offline during builds.
- Static analysis now covers CUDA translation units.
- CycloneDX SBOM generation (`make sbom`).
- Release provenance: every GitHub Release attaches SHA256SUMS, the CycloneDX
  SBOM, THIRD_PARTY_NOTICES derived from it, and build metadata recording the
  image digests behind each artifact; the release body is that version's
  section of this changelog.
- An executed check for every tool family: clippy (deny warnings) gates the
  Rust tools on PRs and nightly, and a shell smoke exercises the ops SDK
  packager.
- `MnaSystem::addTransconductance` stamps a voltage-controlled current
  source. ([#195](https://github.com/apexedgesystems/apex_csf/pull/195))

### Changed

- CI runs a tool change's own role and rebuilds an image only for what the
  change bakes into it; `make test-rust` and `make clippy` take `ROLES=`
  ([#198](https://github.com/apexedgesystems/apex_csf/pull/198)).
- The CI lint job runs in its own image, `apex.lint`: the format and
  secret-scan tools with no C++ or CUDA compiler stack
  ([#198](https://github.com/apexedgesystems/apex_csf/pull/198)).
- Thread pool workers count their in-flight tasks per worker, and the two
  pools share `PoolBase` (`PoolBase.hpp` now holds `PoolStatus`, `TaskError`
  and `PoolConfig`; the pool headers still provide them). Queued tasks drain
  in 24 to 51% less wall time than before on the lab host; `threadsRunning()`
  reads two counts per worker. Breaking for binaries: thread pool, ring-queue
  and `BufferPool` layouts and the pools' `collectErrors()` and
  `threadsRunning()` symbols changed, so rebuild the concurrency and interface
  libraries and everything that uses them together, from matching headers and
  one build type. One source change: remove any use of the debug-only
  `MessageBuffer::acquireTimestamp`, which is gone.
  ([#191](https://github.com/apexedgesystems/apex_csf/pull/191))
- `RingBuffer` keeps its two cursors on one cache line wherever the object
  is placed (48 bytes, 16-byte aligned).
  ([#191](https://github.com/apexedgesystems/apex_csf/pull/191))
- `BufferPool` does no redundant work in `acquire()` or when a buffer's only
  reference is released: an acquire and release pair takes about a third
  less time on the lab host.
  ([#191](https://github.com/apexedgesystems/apex_csf/pull/191))
- `SPSCQueue` sides keep their last view of the other index: on the lab
  host a push and pop pair takes a third less time, and with a backlog
  neither side reads the other's cache line.
  ([#191](https://github.com/apexedgesystems/apex_csf/pull/191))
- `LockFreeQueue` and `ThreadPoolLockFree` state that a push is refused as
  full while a consumer descheduled inside a pop still holds a cell; a
  caller that must not lose the element tries again.
  ([#191](https://github.com/apexedgesystems/apex_csf/pull/191))
- Breaking: every tool lives under its role and installs to
  `bin/tools/<role>/`; the Python package `apex_tools` is now the
  `apex_ops` and `apex_analysis` wheels (scripts set
  `PYTHONPATH=tools/ops/py/apex_ops/src`); `make tools-{py,sh,cpp}` are
  gone ([#178](https://github.com/apexedgesystems/apex_csf/pull/178)).
- Breaking: rebuild C++ consumers of the logs library and adapt typed
  callbacks to `SystemLog::flush(timeoutMs)`.
  ([#188](https://github.com/apexedgesystems/apex_csf/pull/188))
- Breaking: `SystemLog::setNonBlocking` is removed; it had no effect, so
  callers delete the call.
  ([#190](https://github.com/apexedgesystems/apex_csf/pull/190))
- Breaking: the Rust tools are one Cargo workspace at `tools/`, built and
  installed by role. Executables move from `bin/tools/rust/` to
  `bin/tools/<role>/` (build, ops, analysis); `make tools-build` replaces
  `make tools-rust` as what the apex build needs, with `tools-ops` and
  `tools-analysis` on request
  ([#171](https://github.com/apexedgesystems/apex_csf/pull/171)).
- Breaking: scheduler subclasses override `executeCycle(cycle)` in place
  of `executeTick(tick)`, and the multi-thread scheduler's overridable
  dispatch hook is `executeTasksOnCycleMulti(cycle)`; the tick-index
  names remain as non-virtual wrappers. Dependent schedulers migrate and
  rebuild.
- Scheduler task rates: any `freqN / freqD` up to the fundamental is
  admitted, including rates that do not land on whole ticks; those run
  the exact count per `freqD` seconds with spacing uneven by at most one
  tick and a warning in the scheduler log. Refused: zero fields, a rate
  above the fundamental, an offset at or beyond the first period. A task
  added while the clock runs keeps the phase it would have had from the
  first tick.
- Executive clock: tick deadlines are computed from the tick index on a
  nanosecond grid; the wait is an absolute-deadline sleep on the
  monotonic clock, and the clock thread runs with one nanosecond of
  timer slack. The 1000 Hz ceiling on `clockFrequencyHz` is now stated
  policy rather than an arithmetic guard.
- Breaking: TPRM payload prelude is format A v4; readers reject v3
  payloads. Copy the regenerated `compat/tprm/` set.
- TPRM layout hash: a nested struct contributes only its flattened
  leaves, under either value-TOML spelling. Bodies unchanged;
  `nested_enum` re-stamps as `c77da66d`, so re-pin or regenerate.
- Python tooling manages dependencies with uv instead of poetry (PEP 621
  project metadata, uv.lock); the published wheel's internal version now
  matches the release, and it no longer lists pytest as a runtime
  dependency.
- Release pipeline rebuilt on parallel per-target builder images; a
  dispatch rehearsal runs the same steps as a tag release.
- Build image hierarchy retiered (build-base / dev-base / dev-cuda /
  per-platform); containers run as a non-root user.
- GitHub Actions pinned to commit SHAs with an automated pin guard.

### Removed

- Breaking: `PoolStatus::EXCEPTION_STD` and `PoolStatus::EXCEPTION_UNKNOWN`.
  Pool tasks are `noexcept` delegates and report failure through their return
  code; the statuses were reachable only through the pools' retry of their
  own failed error recording, which is removed with them.
  ([#191](https://github.com/apexedgesystems/apex_csf/pull/191))
- The public factorial helpers (`Factorials.hpp`: `precomputedFactorials`,
  `largeFactorial`); replace direct uses before updating. Breaking.
  ([#167](https://github.com/apexedgesystems/apex_csf/pull/167))
- `MnaSystemSparse::refactorize()` and `refactorizeInPlace()`; call
  `factorize()`, which reuses what still applies. Breaking: see the
  [MNA migration table](src/sim/electronics/algorithms/mna/README.md#10-migration).
  ([#199](https://github.com/apexedgesystems/apex_csf/pull/199))

### Fixed

- Transient solver: `run()` counts its steps (no extra sliver step), dense
  steps take any number of voltage sources, Newton steps that do not
  converge are counted and can be refused, the two-state factors follow
  each state's step length, and `cuda::stepCuda` takes the integration
  method. Breaking: unread `TransientConfig` fields and two unused status
  values are removed and `stepCuda` gains a parameter; see the
  [transient migration table](src/sim/electronics/algorithms/transient/README.md#9-migration).
- The gitleaks binary in the lint and dev images belongs to root, not to
  the user id its release archive records.
  ([#202](https://github.com/apexedgesystems/apex_csf/pull/202))
- Companion models: TRAPEZOIDAL is second order, GEAR2's inductor uses
  Gear's coefficients and follows the step length, and a run starts from the
  operating point with each inductor's current. Breaking: `update` and
  `updateAll` take the integration method and the CUDA evaluators return a
  status; rebuild what uses the companions or the transient solver. See the
  [companions migration table](src/sim/electronics/algorithms/companions/README.md#9-migration)
  ([#200](https://github.com/apexedgesystems/apex_csf/pull/200)).
- MNA solvers: the sparse solver reuses an analysis and factors only for
  their own matrix, the AC sweep and the dense solver's borrowed-storage
  modes handle their edge inputs, and the CUDA batch solver reports each
  system it did not solve and, once it has queued work, waits for its
  stream before it returns.
  Breaking: `solveBatchCustom*` return false for such a batch and mark the
  system with not-a-number; rebuild what uses the sparse or batch solvers
  ([#199](https://github.com/apexedgesystems/apex_csf/pull/199)).
- Corrected nonlinear device stamps, junction currents and FET region
  transitions; `BjtCommonEmitter::computeDC` reports failed solves. Breaking:
  device currents and Level 1 region codes change and six model parameters
  are removed; migration steps are in
  [#195](https://github.com/apexedgesystems/apex_csf/pull/195).
- `executive::toString` names `ERROR_TPRM_INGEST`; it returned
  `UNKNOWN_STATUS` for that code.
  ([#192](https://github.com/apexedgesystems/apex_csf/pull/192))
- Firmware releases fail when the build produces no firmware image.
  ([#192](https://github.com/apexedgesystems/apex_csf/pull/192))
- ASYNC logging: `flush()` waits for every accepted entry's write attempt, a
  cut line keeps its newline, and a failed rotation keeps the log descriptor.
  ([#188](https://github.com/apexedgesystems/apex_csf/pull/188))
- Log rotation keeps every backup: a rotation within the same second as an
  earlier one gets a numeric suffix instead of replacing its backup.
  ([#187](https://github.com/apexedgesystems/apex_csf/pull/187))
- `BufferPool::release()` always returns its buffer: the pool lost a buffer
  for good when a release met a thread descheduled inside `acquire()`.
  ([#191](https://github.com/apexedgesystems/apex_csf/pull/191))
- `LockFreeQueue::sizeApprox()` never exceeds `capacity()` and stays correct
  when the queue's indices wrap.
  ([#191](https://github.com/apexedgesystems/apex_csf/pull/191))
- `LockFreeQueue` push and pop compare sequences without the signed
  overflow they had once the cursors pass 2^63, and a pair takes 15% less
  time. ([#191](https://github.com/apexedgesystems/apex_csf/pull/191))
- `SPSCQueue` and `RingBuffer` store elements at `alignof(T)`, over-aligned
  types included, and `SPSCQueue::sizeApprox()` never exceeds `capacity()`
  when read from a third thread.
  ([#189](https://github.com/apexedgesystems/apex_csf/pull/189))
- Concurrency primitives: pool start-up, shutdown and signal handling,
  Barrier and Latch semantics, LockFreeQueue cell alignment and
  SpinLock::isLocked; the UBSan-only check joins the nightly. Breaking:
  rebuild dependent binaries with `utilities_concurrency`, and check
  `isValid()` after constructing a pool, which no longer throws when a
  worker cannot start ([#182](https://github.com/apexedgesystems/apex_csf/pull/182)).
- Build images ship ninja 1.13.1, which rebuilds an object whose header changed
  while its compile was running; it answers at the distro path too, so an
  existing build tree keeps building and rebuilds once as its build log starts
  over. ([#175](https://github.com/apexedgesystems/apex_csf/pull/175))
- The pre-commit node hooks run on a node baked into the dev image instead of
  one downloaded at hook install; hooks run outside the container need node
  and npm on PATH. Workflows run on `ubuntu-24.04`. ([#175](https://github.com/apexedgesystems/apex_csf/pull/175))
- The zephyr dev image builds again on Ubuntu 24.04 (its Python tool install
  passes `--break-system-packages`).
  ([#175](https://github.com/apexedgesystems/apex_csf/pull/175))
- DataTransform holds an applied mask on each hold tick while its entry
  stays armed, and the action demo schedules the hold after its sensor's
  step, so an injected fault stays visible to the watchpoints
  ([#151](https://github.com/apexedgesystems/apex_csf/pull/151)).
- The action demo's fault campaign chains to its cleanup sequence instead
  of restarting itself, and its checkout waits on the action component's
  new GET_RUNNING query, the count of sequences running when its last
  cycle ended ([#151](https://github.com/apexedgesystems/apex_csf/pull/151)).
- The executive's action phase waits for the cycle's in-flight tasks, and
  one thread frames both pushed telemetry and command replies. Breaking:
  `TaskEntry::isRunning` is replaced by an in-flight count; migrate to
  `stillRunning()`, `inFlightCount()`, `markDispatched()`,
  `markCompleted()` and `unmarkDispatched()`. Copies of
  `ApexInterface::stats()` must use `statsSnapshot()` (the counters are
  atomics now), and `getHealthPacket()` returns a value to own rather
  than a reference to borrow; rebuild dependent code
  ([#151](https://github.com/apexedgesystems/apex_csf/pull/151)).
- Improve scalar Legendre accuracy, make the simple scalar API
  allocation-free, and clamp transformed inputs after transformation.
  ([#167](https://github.com/apexedgesystems/apex_csf/pull/167))
- Improve CPU and GPU Legendre triangle accuracy near the poles.
  ([#167](https://github.com/apexedgesystems/apex_csf/pull/167))
- Fixed watchpoint and group debounce: one dispatch after `minFireCount`
  consecutive true evaluations, with group settings kept across
  activation. ([#162](https://github.com/apexedgesystems/apex_csf/pull/162))
- Data manifests naming a header at the wrong path: SystemMonitor's
  dictionary generates again, SystemComponent's `ComponentType` enum carries
  its values, and a missing declared header now fails generation
  ([#166](https://github.com/apexedgesystems/apex_csf/pull/166)).
- Scheduler task rates with a denominator above one ran at the wrong
  rate: the denominator was applied twice (placement stride and a
  per-visit counter), so `100/2` at 1000 Hz ran at 25 Hz instead of 50
  and `100/3` at about 11 Hz instead of 33.3; a long-period task's
  first run came on its `freqD`-th visit rather than at its offset, and
  an offset beyond the fundamental was accepted and never ran.
- Executive clock: a fundamental frequency that does not divide 1000 ran
  at the wrong rate silently (600 Hz ticked at 1000 Hz, 300 Hz at
  333 Hz) because the frame period was held in whole milliseconds; the
  boot log and health packet reported the requested rate throughout.

### Security

- `rustix` 1.1.6 in the Rust tools lock (RUSTSEC-2026-0486), and a seven-day
  dependency cooldown for relocks of the Python tools.
  ([#186](https://github.com/apexedgesystems/apex_csf/pull/186))
- Python tool dependencies re-locked under a 3.11 floor (pillow 13, filelock
  4.2, virtualenv 21.15) and block-buffer 0.12.1 in the Rust lock. ([#175](https://github.com/apexedgesystems/apex_csf/pull/175))
- Python tooling lock: virtualenv 21.6.1 to 21.14.5, clearing four
  published advisories.

## [0.0.1] - 2026-03-27

Initial release: real-time executive (Linux and bare-metal MCU),
communication protocol stack, RT-safe cryptography, simulation (gravity,
analog, GPU compute), Monte Carlo execution, math utilities, and the
multi-platform Docker/CMake/Make build system.

[Unreleased]: https://github.com/apexedgesystems/apex_csf/compare/v0.0.1...HEAD
[0.0.1]: https://github.com/apexedgesystems/apex_csf/releases/tag/v0.0.1
