# CPU Sim Demo

Intel 4004 CPU simulator demo at three production fidelity levels. Each
level swaps the underlying device physics while reusing the same circuit
topology and instruction stream, so customers can pick the tradeoff that
fits their analysis.

## Fidelity Levels

| Level | Description                                                         |
| ----- | ------------------------------------------------------------------- |
| L0    | Behavioral CPU (fast functional model)                              |
| L1    | Component hybrid: 1305 NOR gates at Level 1 Shichman-Hodges, 222    |
|       | pass gates and 610 dynamic storage at binary switch, 105 standalone |
|       | loads as resistive G_LOAD, behavioral timing injection              |
| L2    | Engineered physics: BSIM3 smooth Vgst_eff on the 338 cross-coupled  |
|       | latch transistors, Meyer intrinsic + overlap caps, 66 layout-       |
|       | extracted bootstrap caps loaded from data, behavioral overlay OFF   |

Multi-byte execution uses the L0/L1/L2 hybrid: L0 is authoritative for
instruction state and a fresh transistor circuit is built per byte
(seeded from L0's prior ACC) for transistor-level visibility.

## Building

```bash
make compose-debug
```

## Running

```bash
./build/hosted-x86_64-debug/bin/ApexCpuSimDemo                 # L0+L1 default
./build/hosted-x86_64-debug/bin/ApexCpuSimDemo --level 0       # L0 only
./build/hosted-x86_64-debug/bin/ApexCpuSimDemo --level 2       # L0+L2 engineered
./build/hosted-x86_64-debug/bin/ApexCpuSimDemo --program "D5 00 D3"
./build/hosted-x86_64-debug/bin/ApexCpuSimDemo --probe ACC.0 --probe D0
```

## See Also

- [docs/HOW_TO_RUN.md](docs/HOW_TO_RUN.md) -- step-by-step run guide.
- [examples/](examples/) -- annotated programs to run with `--program`.
