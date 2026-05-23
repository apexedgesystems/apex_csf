# sim_electronics

**Namespace:** `sim::electronics`
**Platform:** Linux-only (CUDA optional)
**C++ Standard:** C++23

Apex's electronics simulation suite. Provides everything needed to build
and analyse analog and digital circuits at user-selectable fidelity,
from a single-line boolean truth table up to full BSIM3 transistor
physics.

---

## Table of Contents

1. [Quick Links](#1-quick-links)
2. [Design Principles](#2-design-principles)
3. [Domains](#3-domains)
4. [Building](#4-building)
5. [Dependencies](#5-dependencies)
6. [Testing](#6-testing)
7. [See Also](#7-see-also)

---

## 1. Quick Links

| Domain                       | Purpose                       | Key Modules                                                                                                                                                                                                                   |
| ---------------------------- | ----------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [algorithms](algorithms/)    | Numerical methods             | [mna](algorithms/mna/README.md), [nonlinear](algorithms/nonlinear/README.md), [transient](algorithms/transient/README.md), [companions](algorithms/companions/README.md), [spice/ngspice](algorithms/spice/ngspice/README.md) |
| [circuit](circuit/README.md) | Construction + simulation API | `Circuit`, `CircuitNet`                                                                                                                                                                                                       |
| [devices](devices/)          | Physical device models        | [linear](devices/linear/README.md), [nonlinear](devices/nonlinear/README.md), [composite](devices/composite/README.md), [descriptors](devices/descriptors/README.md)                                                          |
| [topologies](topologies/)    | Pre-built circuits            | [gates](topologies/gates/README.md), [filters](topologies/filters/README.md), [amplifiers](topologies/amplifiers/README.md)                                                                                                   |
| [chips](chips/)              | Chip-scale assemblies         | [intel4004](chips/intel4004/README.md)                                                                                                                                                                                        |

---

## 2. Design Principles

| Annotation      | Meaning                                                      |
| --------------- | ------------------------------------------------------------ |
| **RT-safe**     | No allocation, bounded execution, safe for real-time loops   |
| **NOT RT-safe** | May allocate or have unbounded I/O; call from non-RT context |

- **Solve / device / topology split.** `algorithms/` is _how to solve_,
  `devices/` is _what is being solved_, `topologies/` is _ready-made
  circuits_, `chips/` is _chip-scale assemblies_. Companion models live
  under `algorithms/` because they are a numerical method (discretizing
  `I = C dV/dt`), not a physical device.
- **Multi-fidelity.** Each device family ships a ladder of models so the
  caller picks the speed / accuracy trade-off, and every level uses the
  same `Circuit` API so swapping fidelity is a parameter change, not a
  rewrite.
- **Header-only where possible.** Algorithms, devices, topologies, and
  the Circuit API are all header-only. CUDA backends are the only
  compiled components.
- **No exceptions in stamps.** Every device stamp is `noexcept` and
  reports failures through the solver's status enum so the Newton-Raphson
  loop never unwinds.

---

## 3. Domains

### algorithms/

| Module                                              | Role                                                                        |
| --------------------------------------------------- | --------------------------------------------------------------------------- |
| [mna](algorithms/mna/README.md)                     | Modified Nodal Analysis solver (sparse KLU, dense LAPACK, AC, batched CUDA) |
| [nonlinear](algorithms/nonlinear/README.md)         | Newton-Raphson framework used by every nonlinear device stamp               |
| [transient](algorithms/transient/README.md)         | Time-domain integration loop (Backward Euler / Trapezoidal / GEAR2)         |
| [companions](algorithms/companions/README.md)       | `Geq` + `Ieq` companion stamps for capacitors and inductors                 |
| [spice/ngspice](algorithms/spice/ngspice/README.md) | NGSPICE bridge for cross-validation (verification only)                     |

### circuit/

The [Circuit](circuit/README.md) module is the user-facing builder. It
allocates nets, registers stamp callbacks for primitive elements, and
delegates simulation to the algorithms layer. Every model in `devices/`,
`topologies/`, and `chips/` composes the Circuit API.

### devices/

| Module                                       | Captures                                                   |
| -------------------------------------------- | ---------------------------------------------------------- |
| [linear](devices/linear/README.md)           | R / L / C primitives plus reactance / impedance helpers    |
| [nonlinear](devices/nonlinear/README.md)     | Diodes, MOSFETs (binary switch through BSIM3), JFETs, BJTs |
| [composite](devices/composite/README.md)     | CMOS composite primitives (Inverter, NAND, NOR)            |
| [descriptors](devices/descriptors/README.md) | Topology-only descriptors with no attached physics         |

#### MOSFET ladder

| Header                   | Fidelity                                       | Pick when                                              |
| ------------------------ | ---------------------------------------------- | ------------------------------------------------------ |
| `MosfetBinarySwitch.hpp` | ON / OFF switch                                | Initialization, warmup, digital-only logic             |
| `MosfetLevel1.hpp`       | Shichman-Hodges 3-region                       | General analog work                                    |
| `MosfetLevel2.hpp`       | SPICE Level 2 (geometry + velocity saturation) | Short-channel circuits                                 |
| `MosfetLevel3.hpp`       | SPICE Level 3 (DIBL + short-channel)           | Sub-micron / low-voltage analog                        |
| `MosfetBsim3.hpp`        | BSIM3 (smooth `Vgst_eff` across all regions)   | Moderate-inversion accuracy (Intel 4004 L2 latch core) |

#### Diode ladder

| Header              | Fidelity                             | Use when                           |
| ------------------- | ------------------------------------ | ---------------------------------- |
| `DiodeShockley.hpp` | Exponential I-V                      | Rectifiers, clamps, basic analog   |
| `DiodeSpice.hpp`    | SPICE diode (series R, junction cap) | Power diodes, accurate transient   |
| `SchottkyDiode.hpp` | Schottky barrier diode               | Fast rectifiers, low-voltage power |
| `ZenerDiode.hpp`    | Forward + breakdown regions          | Voltage regulators / references    |

#### JFET / BJT

| Header             | Fidelity                          | Use when                                    |
| ------------------ | --------------------------------- | ------------------------------------------- |
| `JfetShichman.hpp` | Shichman-Hodges 3-region          | Precision op-amps, analog switches          |
| `JfetLevel2.hpp`   | Adds gate leakage and capacitance | High-frequency / low-noise amplifiers       |
| `BjtEbersMoll.hpp` | Ebers-Moll 4-parameter            | Op-amps and the `BjtCommonEmitter` topology |

### topologies/

| Module                                        | Topologies                                                                                    |
| --------------------------------------------- | --------------------------------------------------------------------------------------------- |
| [gates](topologies/gates/README.md)           | Boolean and transistor-level NOT / AND / OR / NAND / NOR / XOR / XNOR plus half / full adders |
| [filters](topologies/filters/README.md)       | `RcLowPass` (closed-form helpers + simulated step / magnitude response)                       |
| [amplifiers](topologies/amplifiers/README.md) | `BjtCommonEmitter` (fixed-bias NPN common-emitter amplifier)                                  |

### chips/

| Chip                                   | Levels                                                                                             |
| -------------------------------------- | -------------------------------------------------------------------------------------------------- |
| [intel4004](chips/intel4004/README.md) | L0 functional CPU, L1 component hybrid, L2 engineered physics, L3 pure physics (future aspiration) |

---

## 4. Building

The suite is included as a sub-library of the Apex build. Linking is by
target name:

```cmake
target_link_libraries(my_target
  PRIVATE
    sim_electronics_circuit
    sim_electronics_algorithms_mna
    sim_electronics_algorithms_transient
    sim_electronics_devices_nonlinear
)
```

CUDA backends (e.g. `sim_electronics_algorithms_mna_cuda`,
`sim_electronics_devices_nonlinear_cuda`) are built when the project's
CUDA toolkit is detected. Every CUDA target compiles to a no-op when the
toolkit is absent so consumers can link unconditionally.

---

## 5. Dependencies

| Dependency                      | Purpose                                                                |
| ------------------------------- | ---------------------------------------------------------------------- |
| LAPACK / BLAS                   | Dense MNA solve (`dgesv`, `dgetrf`, `dgetrs`)                          |
| KLU (SuiteSparse)               | Sparse MNA solve                                                       |
| CUDA Toolkit (cuSOLVER, cuBLAS) | Optional GPU acceleration                                              |
| libngspice                      | Optional, used only by `algorithms/spice/ngspice` for cross-validation |
| `fmt`                           | String formatting in examples and CLI tools                            |

---

## 6. Testing

Run the standard Docker workflow:

```bash
make compose-debug
make compose-testp
```

Filter by label:

```bash
docker compose run --rm -T dev-cuda \
  ctest --test-dir build/native-linux-debug -L sim_electronics
```

---

## 7. See Also

- [apps/apex_circuit_demo](../../../apps/apex_circuit_demo/) - Reference template for building custom circuits (`gates`, `rc-lowpass`, `common-emitter`)
- [apps/apex_cpu_sim_demo](../../../apps/apex_cpu_sim_demo/) - Intel 4004 CPU demo across L0 / L1 / L2 plus the example `.4004` programs
- Per-module READMEs (linked from [Section 3](#3-domains)) for API details and per-device tables
