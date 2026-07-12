# How to Run: Apex Circuit Demo

Step-by-step commands to build and run the general-purpose electronics
circuit demonstration (CMOS logic gates + analog filters).

---

## 1. Build

```bash
make compose-debug
```

The executable is at `build/native-linux-debug/bin/ApexCircuitDemo`.

---

## 2. Run the Demo

### Default (CMOS gate truth tables)

```bash
docker compose run --rm -T dev-cuda bash -c '
  ./build/native-linux-debug/bin/ApexCircuitDemo
'
```

### CMOS gates (transistor-level truth tables)

```bash
docker compose run --rm -T dev-cuda bash -c '
  ./build/native-linux-debug/bin/ApexCircuitDemo --circuit gates
'
```

### RC low-pass filter (transient step response)

```bash
docker compose run --rm -T dev-cuda bash -c '
  ./build/native-linux-debug/bin/ApexCircuitDemo --circuit rc-lowpass
'
```

### BJT common-emitter amplifier (DC operating point)

```bash
docker compose run --rm -T dev-cuda bash -c '
  ./build/native-linux-debug/bin/ApexCircuitDemo --circuit common-emitter
'
```

### Custom RC component values

```bash
docker compose run --rm -T dev-cuda bash -c '
  ./build/native-linux-debug/bin/ApexCircuitDemo \
    --circuit rc-lowpass --r 10e3 --c 1e-6
'
```

### Custom RC stimulus

```bash
docker compose run --rm -T dev-cuda bash -c '
  ./build/native-linux-debug/bin/ApexCircuitDemo \
    --circuit rc-lowpass --vstep 3.3 --duration 5e-3 --steps 200
'
```

### Common-emitter with custom bias

```bash
docker compose run --rm -T dev-cuda bash -c '
  ./build/native-linux-debug/bin/ApexCircuitDemo \
    --circuit common-emitter --vcc 9 --rc-collector 470 --rb-base 47e3
'
```

### CLI flags

| Flag                  | Default | Description                                                   |
| --------------------- | ------- | ------------------------------------------------------------- |
| `--circuit KIND`      | `gates` | Circuit to simulate (`gates`, `rc-lowpass`, `common-emitter`) |
| `--r OHMS`            | `1e3`   | RC filter resistance (`rc-lowpass`)                           |
| `--c FARADS`          | `1e-6`  | RC filter capacitance (`rc-lowpass`)                          |
| `--vstep VOLTS`       | `5.0`   | RC step input voltage (`rc-lowpass`)                          |
| `--duration SEC`      | `5e-3`  | RC simulation duration (`rc-lowpass`)                         |
| `--steps N`           | `100`   | RC report rows (`rc-lowpass`)                                 |
| `--vcc VOLTS`         | `12`    | Common-emitter supply voltage                                 |
| `--rc-collector OHMS` | `1e3`   | Common-emitter collector resistor                             |
| `--rb-base OHMS`      | `100e3` | Common-emitter base resistor                                  |
| `-h, --help`          |         | Show help                                                     |

---

## 3. What It Demonstrates

The demo is a deliberately small, two-mode example showing how to build
custom circuit simulations on top of the apex electronics libraries
without any chip-specific tooling. The same composition pattern applies
to any user-defined topology.

### `--circuit gates`

Seven CMOS logic gate topologies are constructed from real MOSFETs and
solved through MNA with `MosfetLevel1` (Shichman-Hodges) physics. Each
gate produces actual output voltages for every input combination, not
boolean 0/1.

Gate transistor counts:

| Gate | MOSFETs | Composition                     |
| ---- | ------: | ------------------------------- |
| NOT  |       2 | 1 PMOS + 1 NMOS                 |
| NAND |       4 | 2 PMOS parallel + 2 NMOS series |
| NOR  |       4 | 2 PMOS series + 2 NMOS parallel |
| AND  |       6 | NAND + NOT buffer               |
| OR   |       6 | NOR + NOT buffer                |
| XOR  |      16 | 4 NAND gates                    |
| XNOR |      18 | XOR + NOT buffer                |

### `--circuit rc-lowpass`

A first-order RC low-pass filter. Two analyses:

1. **Step response** -- Backward-Euler transient simulation compared
   against the closed-form analytical solution
   `V_out(t) = V_in * (1 - exp(-t/tau))`. Reports simulated voltage,
   analytical voltage, and percent error at each time step.

2. **Magnitude response** -- Analytical frequency response `|H(jw)|` at
   key frequencies (DC, f_c/10, f_c/2, f_c, 2*f_c, 10*f_c) with decibel
   values. Confirms -3 dB at the cutoff frequency.

### `--circuit common-emitter`

A fixed-bias NPN common-emitter amplifier (3 nets: VCC, COLLECTOR,
BASE; emitter at ground) using the `BjtEbersMoll` device model.
Newton-Raphson DC solve produces the operating point:

- `V_B`, `V_C`, `V_CE` (target ~ VCC/2 for max small-signal swing)
- `I_C`, `I_B` (collector and base bias currents)
- Region check: `cutoff` / `near-saturation` / `active` / `saturation`

Demonstrates active-analog circuit composition: one BJT, two
resistors, one supply -- and the same Circuit + MnaSystem solve as
the digital gates.

---

## 4. Expected Output

### `--circuit gates`

```
=======================================================
  CMOS Digital Logic Gates
  Transistor-Level Circuit Simulation
=======================================================
  VDD = 5.0V, NMOS Kp = 120 uA/V^2, PMOS Kp = 60 uA/V^2
  Vth = 0.7V, W = 10 um, L = 1 um

  CMOS NOT / Inverter (2 MOSFETs):
    Vin (V)  | Vout (V)
    ---------+---------
       0.00  |  5.0000
       1.25  |  4.8893
       2.50  |  0.5766
       3.75  |  0.0274
       5.00  |  0.0000

  CMOS NAND (4 MOSFETs):
    VinA (V) | VinB (V) | Vout (V)
    ---------+----------+---------
       0.00  |     0.00 |  5.0000
       0.00  |     5.00 |  5.0000
       5.00  |     0.00 |  4.9999
       5.00  |     5.00 |  0.0000
  ...
```

### `--circuit rc-lowpass`

```
=======================================================
  RC Low-Pass Filter
  Transient step response vs analytical solution
=======================================================
  R          = 1000 ohm
  C          = 1e-06 F
  tau = R*C  = 0.001 s
  f_c        = 159.2 Hz
  V_step     = 5 V
  duration   = 0.005 s (5.00 tau)

--- Step Response ---
       t (s)         t/tau     V_sim (V)   V_analytic    error %
  ----------  ------------  ------------ ------------  ---------
           0        0.0000      0.000000     0.000000     0.0000
       5e-05        0.0500      0.243260     0.243853     0.2430
       ...

--- Magnitude Response ---
      f (Hz)  |H(jomega)|         dB
  ----------  ----------  ---------
           0    1.000000     0.0000
       15.92    0.995037    -0.0432
       79.58    0.894427    -0.9691
       159.2    0.707107    -3.0103
       318.3    0.447214    -6.9897
        1592    0.099504   -20.0432
```

---

## 5. How Apex Composes a Custom Circuit

Both modes follow the same composition pattern customers use to build
their own simulations:

```cpp
// 1. Choose a device model (or compose your own).
MosfetLevel1Params nmos{.Kp = 120e-6, .Vth = 0.7, .lambda = 0.02};

// 2. Describe the circuit topology with the Circuit API.
CmosNandCircuit gate(VDD, W, L, nmos, pmos);
gate.build();

// 3. Drive inputs and read outputs.
gate.setInputs(5.0, 5.0);
double vOut = gate.computeDC();
```

For transient analyses (filters, oscillators, dynamic gates):

```cpp
RcLowPass filter(R, C);
filter.build();
TransientState state;
state.resize(filter.circuit().netCount(), 0);
filter.circuit().computeDC(state);                    // DC operating point
filter.setInputVoltage(VSTEP);                        // apply stimulus
filter.circuit().solver().setIntegrationMethod(
    IntegrationMethod::BACKWARD_EULER);
for (int s = 0; s < N; ++s) {
  filter.circuit().solver().step(DT, state);          // integrate
}
```

The same three-step pattern -- pick devices, describe topology, drive
the analysis -- scales from small primitive cells (this demo) up to the
full Intel 4004 (see `apex_cpu_sim_demo`).

---

## 6. Run Unit Tests

```bash
make compose-testp
```

---

## Troubleshooting

| Problem                     | Fix                                                |
| --------------------------- | -------------------------------------------------- |
| `unknown --circuit '...'`   | Use `gates`, `rc-lowpass`, or `common-emitter`     |
| RC error % values are large | Increase `--steps` for finer report granularity    |
| RC simulation diverges      | Check that R and C produce a reasonable tau        |
| All RC output is 0V         | Verify `--vstep` is nonzero                        |
| Gate output is mid-rail     | Vth or W/L not balanced; tune `MosfetLevel1Params` |
