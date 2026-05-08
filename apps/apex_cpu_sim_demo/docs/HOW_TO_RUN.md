# How to Run: Intel 4004 CPU Simulator Demo

Step-by-step commands to build and run the Intel 4004 CPU simulator at
behavioral (L0), component-hybrid (L1), and engineered-physics (L2)
fidelity.

---

## 1. Build

```bash
make compose-debug
```

The executable is at `build/native-linux-debug/bin/ApexCpuSimDemo`.

---

## 2. Run the Demo

### Default (LDM 5 at L0 + L1)

```bash
docker compose run --rm -T dev-cuda bash -c '
  ./build/native-linux-debug/bin/ApexCpuSimDemo
'
```

### L0 behavioral only (sub-microsecond per byte)

```bash
docker compose run --rm -T dev-cuda bash -c '
  ./build/native-linux-debug/bin/ApexCpuSimDemo --level 0
'
```

### L0 + L2 engineered physics (BSIM3 + Meyer caps + bootstrap caps)

```bash
docker compose run --rm -T dev-cuda bash -c '
  ./build/native-linux-debug/bin/ApexCpuSimDemo --level 2
'
```

### Custom program

```bash
docker compose run --rm -T dev-cuda bash -c '
  ./build/native-linux-debug/bin/ApexCpuSimDemo --program "D5 00 D3"
'
```

### List or run a built-in example

```bash
docker compose run --rm -T dev-cuda bash -c '
  ./build/native-linux-debug/bin/ApexCpuSimDemo --list-examples
'

docker compose run --rm -T dev-cuda bash -c '
  ./build/native-linux-debug/bin/ApexCpuSimDemo --level 0 --example modulus
'
```

### Probe specific nets at L1 or L2

```bash
docker compose run --rm -T dev-cuda bash -c '
  ./build/native-linux-debug/bin/ApexCpuSimDemo --level 2 --probe ACC.0 --probe D0
'
```

### CLI flags

| Flag                    | Default          | Description                                              |
| ----------------------- | ---------------- | -------------------------------------------------------- |
| `--level N`             | `1`              | `0` = L0 behavioral, `1` = L0 + L1 hybrid, `2` = L0 + L2 |
| `--behavioral-only`     | -                | Alias for `--level 0`                                    |
| `--netlist PATH`        | built-in 4004    | Path to SPICE netlist file                               |
| `--bootstrap-caps PATH` | built-in caps    | Bootstrap-cap data file (L2 only)                        |
| `--program "HEX"`       | LDM 5            | Program as space-separated hex bytes                     |
| `--example NAME`        | -                | Load a canned example by name or path (see Section 7)    |
| `--list-examples`       | -                | List the canned examples and exit                        |
| `--probe NET`           | 20 default nets  | Probe a net name (repeatable, L1/L2 only)                |
| `--warmup N`            | `16`             | Warmup NOP count for L1/L2 timing stabilization          |
| `-h, --help`            |                  | Show help                                                |

---

## 3. Fidelity Levels

| Level | Per-byte cost | Physics                                              | Use when                                                |
| ----- | ------------: | ---------------------------------------------------- | ------------------------------------------------------- |
| L0    |       sub-us  | Behavioral CPU (truth-table opcodes)                 | You need fast functional execution                      |
| L1    |        ~2.5 s | Shichman-Hodges + binary switch + behavioral overlay | You want transistor voltages with stable per-byte state |
| L2    |        ~4.6 s | BSIM3 + Meyer caps + bootstrap caps, overlay OFF     | You want physics-resolved decode chain + latch dynamics |

### L0: Behavioral CPU

Fast functional model. Executes the full instruction set, reports register
and accumulator state. No circuit simulation.

### L1: Component Hybrid

Transistor-level circuit built from the SPICE netlist:

- 2,242 transistors across 1,081 nets
- 1,305 NOR gates at Level 1 Shichman-Hodges physics
- 222 pass gates and 610 dynamic storage at binary switch
- 105 standalone loads as resistive G_LOAD
- Behavioral timing/decode overlay (L0 authoritative for instruction state)

### L2: Engineered Physics

Same netlist, more physics:

- 338 cross-coupled latch transistors switch to **BSIM3** (smooth
  `Vgst_eff = n*Vt * ln(1 + exp((Vgs - Vth) / (n*Vt)))`) so the latch
  feedback core has a continuous derivative across moderate inversion
- **Meyer intrinsic + overlap capacitances** stamped on every device
  (Cgs, Cgd, Cgb)
- **66 layout-extracted bootstrap caps** loaded from a data file and
  stamped between gate (intermediate node) and source (output) of
  bootstrap-load topologies
- **Behavioral overlay OFF** -- decode signals, OPR/OPA latches, and
  ACC writeback are resolved by physics (with custom-physics writeback
  primitives that abstract the non-converging multi-stage Vth-drop
  cascades)

L0 remains authoritative for instruction state in the multi-byte hybrid
flow; L2's per-byte transistor circuit is built fresh, seeded from L0's
prior ACC, and observed via `traceExecuteByte`.

---

## 4. Expected Output

### L0 only (`--level 0`)

```
=======================================================
  Intel 4004 CPU Simulator
  Level: L0 (behavioral)
=======================================================

Program (1 bytes):
  0000: D5  LDM 5

--- L0: Behavioral CPU ---
  ACC = 5 (0b0101)
  CY  = 0

=======================================================
  L0 simulation complete.
=======================================================
```

### L0 + L1 (default)

```
Program (1 bytes):
  0000: D5  LDM 5

--- L0: Behavioral CPU ---
  ACC = 5 (0b0101)
  CY  = 0

--- L1: Transistor Circuit (L0/L1 hybrid) ---
  Loading netlist: src/sim/electronics/intel4004/netlist/data/lajos-4004.spice
  Transistors: 2242
  Warmup NOPs per byte: 16

  Byte 0 (0xD5 LDM 5): L0 ACC=5 (auth), L1 ACC=5 [OK]

--- Net Voltages (final byte, L1 state) ---
  ACC.0        =   0.000V  [L]
  ACC.1        =   5.000V  [H]
  ACC.2        =   0.000V  [L]
  ACC.3        =   5.000V  [H]
  ...
```

### L0 + L2 (`--level 2`)

```
--- L2: Engineered Physics (L0/L2 hybrid) ---
  Loading netlist:        src/sim/electronics/intel4004/netlist/data/lajos-4004.spice
  Loading bootstrap caps: src/sim/electronics/intel4004/netlist/data/lajos-4004-bootstrap-caps.txt
  Transistors: 2242 (338 latch core via BSIM3)
  Meyer caps: ON (intrinsic + overlap on every device)
  Warmup NOPs per byte: 16
  Steps per phase: 5 (caps need finer dt than L1)

  Byte 0 (0xD5 LDM 5): L0 ACC=5 (auth), L2 ACC=5 [OK]

--- Net Voltages (final byte, L2 state) ---
  ACC.0        =   0.000V  [L]
  ACC.1        =   5.000V  [H]
  ACC.2        =   0.000V  [L]
  ACC.3        =   5.000V  [H]
  ...
```

---

## 5. How Apex Composes a Tiered CPU Simulator

The demo shows how to compose three apex libraries to build a custom
CPU simulator with selectable fidelity:

```cpp
// 1. Authoritative behavioral model -- fast, easy to validate.
Intel4004Cpu behavioral;
behavioral.loadProgram(program.data(), program.size());
for (auto& byte : program) behavioral.step();

// 2. Per-byte transistor circuit, seeded from L0 state.
auto netlist = loadSpiceNetlist(netlistPath);
Intel4004GridLevel2 grid;       // pick a fidelity (L1, L2, ...)
grid.enableMeyerCaps_ = true;
auto circuit = grid.buildCircuit(netlist);
grid.loadBootstrapCaps(capsPath);

auto state = grid.simulateLevel1FromScratch(
    circuit, rom, romSize, warmupNops, /*programBytes=*/0);
grid.forceAccLogic(state.nodeVoltages, prevAcc);
grid.traceExecuteByte(circuit, state, byte, nullptr);

// 3. Read transistor-level voltages on any net.
auto netId = grid.findNet("ACC.0");
double v = state.nodeVoltages[netId];
```

The same hybrid pattern -- authoritative behavioral model + per-byte
fresh transistor circuit -- generalizes to any custom CPU netlist:
swap `lajos-4004.spice` for your own SPICE netlist and pick the
fidelity level you need for the analysis at hand.

---

## 6. Example Algorithms

Each program below runs as `--program "HEX..."`. They're short enough to
finish in seconds at L1/L2, and each illustrates a different facet of
the 4004 ISA.

### Add two 4-bit numbers (5 + 3 = 8)

```bash
docker compose run --rm -T dev-cuda bash -c '
  ./build/native-linux-debug/bin/ApexCpuSimDemo --level 0 \
    --program "D5 B0 D3 80"
'
```

| Step | Hex | Mnemonic | Effect                |
| ---- | --- | -------- | --------------------- |
| 0    | D5  | LDM 5    | ACC = 5               |
| 1    | B0  | XCH R0   | R0 <-> ACC (R0 = 5)   |
| 2    | D3  | LDM 3    | ACC = 3               |
| 3    | 80  | ADD R0   | ACC = ACC + R0 = 8    |

Expected: `ACC = 8`, `R0 = 5`, `CY = 0`.

### 4-bit subtraction with carry semantics (4 - 9, borrow)

```bash
docker compose run --rm -T dev-cuda bash -c '
  ./build/native-linux-debug/bin/ApexCpuSimDemo --level 0 \
    --program "F1 D9 B0 D4 90"
'
```

| Step | Hex | Mnemonic | Effect                                        |
| ---- | --- | -------- | --------------------------------------------- |
| 0    | F1  | CLC      | CY = 0                                        |
| 1    | D9  | LDM 9    | ACC = 9                                       |
| 2    | B0  | XCH R0   | R0 = 9, ACC = 0                               |
| 3    | D4  | LDM 4    | ACC = 4                                       |
| 4    | 90  | SUB R0   | ACC = ACC - R0 - !CY = 4 - 9 - 1 = -6 = 0xA  |

Expected: `ACC = 10` (4-bit wrap of -6), `CY = 0` (the 4004 sets `CY=1`
on no-borrow; here a borrow occurred).

### Increment chain (7 + 3 = 10 via IAC)

```bash
docker compose run --rm -T dev-cuda bash -c '
  ./build/native-linux-debug/bin/ApexCpuSimDemo --level 0 \
    --program "D7 F2 F2 F2"
'
```

| Step | Hex | Mnemonic | Effect    |
| ---- | --- | -------- | --------- |
| 0    | D7  | LDM 7    | ACC = 7   |
| 1-3  | F2  | IAC      | ACC += 1  |

Expected: `ACC = 10`.

### Carry rollover (0xF + 1 = 0x0 with CY=1)

```bash
docker compose run --rm -T dev-cuda bash -c '
  ./build/native-linux-debug/bin/ApexCpuSimDemo --level 0 \
    --program "DF F2"
'
```

Expected: `ACC = 0`, `CY = 1` (carry-out of the 4-bit accumulator).

### Compare across fidelity levels (same program, all three levels)

```bash
docker compose run --rm -T dev-cuda bash -c '
  for L in 0 1 2; do
    echo "=== Level $L ==="
    ./build/native-linux-debug/bin/ApexCpuSimDemo --level $L \
      --program "D5 B0 D3 80" | grep -E "ACC|L. ACC"
  done
'
```

L0 runs in microseconds, L1 takes ~10 s for 4 bytes, L2 takes ~15 s --
all three should agree on `ACC = 8`.

### Run a 2-byte instruction (FIM P0, immediate 0x35)

```bash
docker compose run --rm -T dev-cuda bash -c '
  ./build/native-linux-debug/bin/ApexCpuSimDemo --level 0 \
    --program "20 35"
'
```

Loads register-pair P0 (R0:R1) with the 8-bit immediate `0x35`. The
4004 stores the high nibble in the even register and the low nibble in
the odd register, so `R0 = 3` and `R1 = 5`. The disassembler shows two
single-byte lines, but the L0 CPU correctly consumes both bytes as one
instruction.

> **Tip:** for control-flow programs (`JCN`, `JUN`, `JMS`, `ISZ`, `FIN`)
> use `--level 0` to verify behavior in seconds, then promote to `--level
> 1` or `--level 2` for transistor-level inspection of a specific byte.

---

## 7. Example Program Files (`.4004`)

The demo ships a small set of canned example programs in
[`examples/`](../examples). Each file is a plain-text Intel 4004 ROM
image: comments + hex bytes, exactly the byte sequence that would be
patterned into a 4001 ROM (or burned into a modern EPROM/EEPROM) and
fetched by real 4004 silicon.

### File format

```text
# === <name>.4004 ===
# Description lines (printed by --example before execution)
# ...

# Hex bytes, one or more per line. Anything after `#` is a comment.
F1   # CLC          carry = 0
DD   # LDM 13       ACC = 13
B0   # XCH R0       R0 = 13
```

Rules:

- Each token is one byte in hex (`00`..`FF`).
- `#` to end-of-line is a comment.
- The leading `#` block (before any code byte) is captured as the
  program description and printed by `--example`.
- 2-byte instructions (`JCN`, `FIM`, `JUN`, `JMS`, `ISZ`) are written
  as two consecutive bytes -- they are only one logical instruction
  but two ROM bytes.

### Drop-in custom programs

Customers can add their own programs by dropping a new `.4004` file
into `apps/apex_cpu_sim_demo/examples/` (or anywhere on disk; pass
`--example PATH/TO/file.4004` for an arbitrary location). No rebuild
needed.

### Shipped examples

| Name            | Demonstrates                                                       |
| --------------- | ------------------------------------------------------------------ |
| `add`           | Two-register addition (5 + 3 = 8)                                  |
| `subtract`      | 4-bit borrow semantics (4 - 9 = 0xA, CY = 0)                       |
| `increment`     | Accumulator increment chain (`IAC`)                                |
| `carry_chain`   | 4-bit overflow + `CLC` to drop carry                               |
| `register_pair` | 2-byte `FIM` loading R0:R1 = 3:5 from `0x35`                       |
| `modulus`       | Repeated subtraction loop (13 % 5 = 3) using JCN + JUN             |
| `multiply`      | Repeated addition with ISZ count-up loop (3 * 5 = 15)              |
| `bcd_add`       | Two BCD digits + `DAA` + `TCC` (5 + 7 = 12 BCD: ones=2, tens=1)    |
| `subroutine`    | `JMS` / `BBL` roundtrip through a 3-deep hardware stack            |
| `fibonacci`     | Multi-register state machine: fib(7) = 13                          |
| `rotate`        | `RAL` chain that walks a single bit: 1 -> 2 -> 4 -> 8              |
| `bit_count`     | Popcount of 0xB via `RAR` + `JCN` per-bit test (= 3)               |
| `kbp`           | `KBP` (Keyboard Process) one-hot to bit-position mapping           |
| `ram_io`        | `DCL` + `SRC` + `WRM` + `RDM` write-then-read-back to RAM         |
| `indirect`      | `FIN` (table lookup) + `JIN` (computed jump) within a ROM page     |
| `twos_complement` | `CMA` (1's complement) + `IAC` to negate a 4-bit value           |
| `register_inc`  | `INC R` register-direct increment (vs `IAC` on the accumulator)    |
| `flag_ops`      | `CLB` + `CMC` + `DAC` + `TCS` flag/accumulator manipulation        |
| `ram_extended`  | `ADM` + `SBM` + `WR0` / `RD0` + `WMP`: RAM-operand math, status RAM, RAM port |

### Running on real silicon (or any ROM-driven 4004 system)

The byte stream after stripping comments is exactly what the chip
fetches. To turn an example file into a binary ROM image:

```bash
grep -v '^#' add.4004 | grep -oE '[0-9a-fA-F]{2}' | xxd -r -p > add.bin
```

`add.bin` is then a 4-byte ROM image suitable for an EPROM burner or
any modern microcontroller wired as a ROM peripheral to a real 4004.

---

## 8. Supported Instructions

The demo disassembles and can execute these 4004 opcodes:

| Opcode    | Mnemonic | Description               |
| --------- | -------- | ------------------------- |
| `0x00`    | NOP      | No operation              |
| `0xDn`    | LDM n    | Load immediate to ACC     |
| `0x2n`    | FIM Pp,d | Fetch immediate to pair   |
| `0xAn`    | LD Rn    | Load register to ACC      |
| `0xBn`    | XCH Rn   | Exchange ACC and register |
| `0x8n`    | ADD Rn   | Add register to ACC       |
| `0x9n`    | SUB Rn   | Subtract register         |
| `0x6n`    | INC Rn   | Increment register        |
| `0xF0-FB` | CLB..DAA | Accumulator operations    |

---

## 9. Run Unit Tests

```bash
make compose-testp
```

---

## Troubleshooting

| Problem                       | Fix                                                                       |
| ----------------------------- | ------------------------------------------------------------------------- |
| `--level must be 0, 1, or 2`  | Only levels 0, 1, and 2 are supported                                     |
| L1/L2 shows `[NaN/Inf]`       | Circuit convergence issue; try increasing `--warmup`                      |
| L1 ACC differs from L0        | Expected for some opcodes (charge retention limitation; see L2)           |
| L2 takes forever              | Each byte ~3-5 s; multi-byte programs scale linearly. Use `--level 1` for fast iteration |
| `Bootstrap caps file missing` | Provide `--bootstrap-caps PATH` or run from project root for the default  |
| Netlist not found             | Verify `--netlist` path relative to working directory                     |
