# Circuit Demo

General-purpose electronics circuit demonstration. Selects between
transistor-level CMOS logic gates and analog filters via a single
`--circuit` flag, exercising the apex Circuit + MNA + transient
infrastructure without any Intel 4004-specific tooling.

Demonstrates the composition pattern customers use to build their own
circuit simulations: pick device models from `sim_electronics_devices_*`,
describe a topology with `sim_electronics_circuit`, and analyse with
`sim_electronics_algorithms_transient` (transient) or
`MnaSystem::computeDC()` (DC operating point).

## Circuits

| Selector     | Topology                                    | Analysis       |
| ------------ | ------------------------------------------- | -------------- |
| `gates`      | CMOS NOT / NAND / NOR transistor-level      | Transient      |
| `rc-lowpass` | First-order RC low-pass (configurable R, C) | Transient step |

## Building

```bash
make compose-debug
```

## Running

```bash
./build/hosted-x86_64-debug/bin/ApexCircuitDemo
./build/hosted-x86_64-debug/bin/ApexCircuitDemo --circuit gates
./build/hosted-x86_64-debug/bin/ApexCircuitDemo --circuit rc-lowpass --r 10e3 --c 1e-6
./build/hosted-x86_64-debug/bin/ApexCircuitDemo --circuit rc-lowpass --vstep 3.3 --duration 5e-3
```

## See Also

- [docs/HOW_TO_RUN.md](docs/HOW_TO_RUN.md) -- step-by-step run guide with
  example output for each circuit.
