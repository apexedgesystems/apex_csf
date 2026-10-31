# Changelog

All notable changes to Apex CSF are documented in this file. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
While the major version is 0, minor releases may contain breaking changes;
any such change is called out explicitly in its entry.

## [Unreleased]

### Added

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

### Changed

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

- The public factorial helpers (`Factorials.hpp`: `precomputedFactorials`,
  `largeFactorial`); replace direct uses before updating. Breaking.
  ([#167](https://github.com/apexedgesystems/apex_csf/pull/167))

### Fixed

- Build images ship ninja 1.13.1, which rebuilds an object whose header changed
  while its compile was running; it answers at the distro path too, so an
  existing build tree keeps building and rebuilds once as its build log starts
  over. ([#175](https://github.com/apexedgesystems/apex_csf/pull/175))
- The pre-commit node hooks run on a node baked into the dev image instead of
  one downloaded at hook install; hooks run outside the container need node
  and npm on PATH. Workflows run on `ubuntu-24.04`. ([#175](https://github.com/apexedgesystems/apex_csf/pull/175))
- The zephyr dev image builds again on Ubuntu 24.04 (its Python tool install
  passes `--break-system-packages`).
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
