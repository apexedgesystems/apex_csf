# HIL Demo

Hardware-in-the-loop flight demonstration. The POSIX host runs the
plant model and orchestrator; the STM32 runs the flight controller
firmware. The two halves communicate over UART/SLIP and run
simultaneously to exercise full closed-loop control.

## Layout

| Subdir      | Role                                  |
| ----------- | ------------------------------------- |
| `model/`    | POSIX-side plant dynamics library     |
| `exec/`     | POSIX-side orchestrator executable    |
| `firmware/` | STM32-side flight controller firmware |
| `watchdog/` | POSIX-side watchdog process           |
| `driver/`   | UART/SLIP I/O glue                    |
| `support/`  | Shared message types and helpers      |
| `tprm/`     | TPRM configuration files              |

## Building

POSIX (host plant + orchestrator + watchdog):

```bash
make compose-debug
```

STM32 (flight controller firmware):

```bash
make compose-stm32
```

## See Also

- [docs/HIL_DESIGN.md](docs/HIL_DESIGN.md) -- closed-loop architecture
  and message protocol.
- [docs/FRAMEWORK_COMPARISON.md](docs/FRAMEWORK_COMPARISON.md) -- how this
  HIL setup compares to common alternatives.
- [docs/HOW_TO_RUN.md](docs/HOW_TO_RUN.md) -- step-by-step run guide for
  both halves.
- [docs/RESULTS.md](docs/RESULTS.md) -- expected closed-loop behavior and
  values to verify against.
- [docs/DEPLOY_PROCEDURE.md](docs/DEPLOY_PROCEDURE.md) -- on-target
  deployment procedure.
