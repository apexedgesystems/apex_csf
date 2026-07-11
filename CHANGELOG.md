# Changelog

All notable changes to Apex CSF are documented in this file. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
While the major version is 0, minor releases may contain breaking changes;
any such change is called out explicitly in its entry.

## [Unreleased]

### Added

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
- Nightly assurance pipeline: warm firmware builds for every MCU target,
  sanitizer suites, cppcheck, dependency and container scans, and
  per-language coverage floors; CodeQL analysis on the default branch.
- Data-driven platform registry: compose services, CI matrices, and image
  lists are generated from one table and drift-gated in CI.
- Hermetic dependency resolution: Rust crates and Python wheels are baked
  into the build images and resolved offline during builds.
- Static analysis now covers CUDA translation units.
- CycloneDX SBOM generation (`make sbom`).

### Changed

- Release pipeline rebuilt on parallel per-target builder images; a
  dispatch rehearsal runs the same steps as a tag release.
- Build image hierarchy retiered (build-base / dev-base / dev-cuda /
  per-platform); containers run as a non-root user.
- GitHub Actions pinned to commit SHAs with an automated pin guard.

## [0.0.1] - 2026-03-27

Initial release: real-time executive (Linux and bare-metal MCU),
communication protocol stack, RT-safe cryptography, simulation (gravity,
analog, GPU compute), Monte Carlo execution, math utilities, and the
multi-platform Docker/CMake/Make build system.

[Unreleased]: https://github.com/apexedgesystems/apex_csf/compare/v0.0.1...HEAD
[0.0.1]: https://github.com/apexedgesystems/apex_csf/releases/tag/v0.0.1
