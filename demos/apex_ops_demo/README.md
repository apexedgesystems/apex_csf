# Ops Demo

Pure SIL demo for Zenith C2 system development. Configurable waveform
generators emit rich telemetry, exercising every APROTO capability so
ground-side ops tooling can be developed and validated without a
hardware target.

## Layout

| Subdir         | Role                                 |
| -------------- | ------------------------------------ |
| `wave/`        | Waveform generator library           |
| `exec/`        | Orchestrator executable              |
| `test/plugin/` | Test plugin for ops-driven scenarios |
| `tprm/`        | TPRM configuration files             |

## Building

```bash
make compose-debug
```

## See Also

- [docs/OPS_DEMO_DESIGN.md](docs/OPS_DEMO_DESIGN.md) -- waveform generator
  architecture and APROTO usage.
- [docs/HOW_TO_RUN.md](docs/HOW_TO_RUN.md) -- step-by-step run guide.
- [docs/RESULTS.md](docs/RESULTS.md) -- expected telemetry shape per
  scenario.
- [docs/DEPLOY_PROCEDURE.md](docs/DEPLOY_PROCEDURE.md) -- packaging notes.
