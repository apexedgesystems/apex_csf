# Monte Carlo Demo Results

**Model:** LDO Voltage Regulator (3.3V output from 5V input)
**Infrastructure:** `system_core_monte_carlo` library
**Runs:** 50,000 per seed, 3 seeds archived
**Throughput:** ~24M runs/sec on 22 cores (~2ms wall time)

---

## Circuit Under Analysis

A linear voltage regulator with resistive feedback divider, output capacitor
with parasitic ESR, and internal voltage reference.

```
        V_in (5V)
          |
       [LDO pass element]
          |
          +---- V_out (3.3V) ----+
          |                      |
         [R1]                 [C_out + ESR]
          |                      |
          +---- V_fb ----+      GND
          |              |
         [R2]         [V_ref]
          |
         GND
```

The regulator drives V_out such that V_fb = V_ref, giving V_out = V_ref \* (1 + R1/R2).

---

## Component Tolerances

| Parameter           | Nominal     | Tolerance | Distribution     | Rationale                    |
| ------------------- | ----------- | --------- | ---------------- | ---------------------------- |
| R1 (feedback upper) | 100k ohm    | +/- 1%    | Normal (3-sigma) | Standard thin-film resistor  |
| R2 (feedback lower) | 60.606k ohm | +/- 1%    | Normal (3-sigma) | Standard thin-film resistor  |
| C_out (output cap)  | 10uF        | +/- 20%   | Uniform          | Ceramic cap worst-case spec  |
| ESR (cap parasitic) | 10m ohm     | +/- 50%   | Log-normal       | Varies with temp, aging, lot |
| V_ref (reference)   | 1.25V       | +/- 2%    | Normal (3-sigma) | IC process variation         |
| Bandwidth           | 100kHz      | +/- 30%   | Uniform          | Process and load dependent   |

---

## Spec Limits

| Output       | Spec           | Limit           | Rationale                      |
| ------------ | -------------- | --------------- | ------------------------------ |
| V_out        | +/- 3% of 3.3V | 3.201V - 3.399V | Downstream logic Vdd tolerance |
| Ripple       | Upper          | 50mV peak       | ADC/PLL noise budget           |
| Phase margin | Lower          | 45 degrees      | Stability criterion            |

---

## Results Summary (seed=42, N=50,000)

| Output                   | Mean     | Stddev   | P05      | P95      | Yield      |
| ------------------------ | -------- | -------- | -------- | -------- | ---------- |
| V_out                    | 3.313V   | 24.0mV   | 3.273V   | 3.352V   | 99.99%     |
| Ripple                   | 53.3mV   | 11.2mV   | 37.5mV   | 74.3mV   | 44.16%     |
| Settling time            | 384us    | 84us     | 260us    | 534us    | N/A        |
| Phase margin             | 93.9 deg | 1.03 deg | 92.5 deg | 95.9 deg | 100.00%    |
| **Combined (all specs)** |          |          |          |          | **44.16%** |

---

## Key Findings

### 1. Voltage accuracy is excellent

V_out yield is 99.99%. The 1% resistors in the feedback divider keep V_out
well within the +/-3% tolerance band. The distribution is Gaussian with a CV
of 0.72%, meaning the resistor ratio dominates and V_ref variation is secondary.

The histogram shows the distribution sits comfortably between the upper and
lower spec lines with wide margins on both sides.

### 2. Ripple yield is the bottleneck

Only 44% of simulated boards pass the 50mV ripple spec. The ripple distribution
is right-skewed (not Gaussian) because ESR follows a log-normal distribution.
The mean ripple (53.3mV) already exceeds the spec, so more than half of all
boards fail on ripple alone.

This is the finding that single-point nominal simulation would miss entirely.
The nominal ESR of 10m ohm gives a ripple of ~1mV (well under spec), but the
+/-50% ESR tolerance pushes real-world ripple far higher.

### 3. Ripple is the sole yield limiter

Combined yield (44.16%) equals ripple yield exactly. Voltage and phase margin
both pass at effectively 100%. Improving ripple yield is the only path to
improving overall board yield.

### 4. ESR drives ripple, resistors drive voltage (uncorrelated)

The correlation matrix confirms what the physics predicts:

| Pair                     | Correlation | Interpretation                             |
| ------------------------ | ----------- | ------------------------------------------ |
| V_out vs Ripple          | r = 0.001   | Uncorrelated (different parameters)        |
| Ripple vs Settling       | r = 0.86    | Strong (both driven by output RC)          |
| Ripple vs Phase margin   | r = -0.64   | Anti-correlated (ESR zero helps stability) |
| Settling vs Phase margin | r = 0.86    | Strong (same RC dynamics)                  |

V_out is set purely by the R1/R2 ratio and V_ref. Ripple is set by ESR and
capacitance. They can be addressed independently.

### 5. The yield sweep quantifies the design margin gap

From the yield sweep plot for ripple:

| Target Yield | Required Spec Limit | Gap from Current Spec |
| ------------ | ------------------- | --------------------- |
| 90%          | ~60mV               | +10mV (relax spec)    |
| 95%          | ~69mV               | +19mV (relax spec)    |
| 99%          | ~83mV               | +33mV (relax spec)    |

To keep the 50mV spec and reach 95% yield, the design must change.

### 6. Convergence confirms sample size was sufficient

Both V_out and ripple converged (SEM/|mean| < 0.1%) well before 50,000 samples.
The convergence plots show the running mean stabilizing by ~1,000 samples,
with the 2-SEM uncertainty band collapsing to negligible width by ~10,000.

---

## Reproducibility

Results are deterministic for a given seed. The sweep generator seeds each
run's RNG as `(baseSeed + runIndex)`, ensuring identical output regardless
of thread count or execution order.

| Seed  | Ripple Yield | V_out Yield | Combined |
| ----- | ------------ | ----------- | -------- |
| 42    | 44.16%       | 99.99%      | 44.16%   |
| 1337  | 44.13%       | 99.99%      | 44.12%   |
| 99999 | 44.42%       | 99.98%      | 44.41%   |

The ~0.3% variation across seeds is statistical noise from finite sampling,
consistent with the expected standard error.

---

## Design Recommendations

To bring ripple yield above 95% while keeping the 50mV spec:

1. **Tighten ESR tolerance** - Switch from +/-50% to +/-20% capacitors
   (polymer or high-quality MLCC). ESR is the dominant ripple contributor.

2. **Add output capacitance** - Parallel caps reduce effective ESR by 1/N
   and increase total capacitance. Two 10uF caps halve ESR contribution.

3. **Increase capacitor value** - 22uF or 47uF output cap reduces both
   the capacitive charging ripple component and the RC time constant.

These are testable hypotheses. Each can be validated by modifying the model
parameters and re-running the MC sweep.

---

## Archived Runs

Stored as compressed tarballs in `docs/results/`:

```
apps/apex_mc_demo/docs/results/
  run_1.tar.gz   seed=42,    N=50,000   (baseline)
  run_2.tar.gz   seed=1337,  N=50,000   (reproducibility check)
  run_3.tar.gz   seed=99999, N=100,000  (convergence scaling)
```

Extract with `tar xzf run_N.tar.gz`. Each archive contains:

- `results.csv` - Per-run output values (one row per simulation)
- `summary.csv` - Statistics per output field
- 14 PNG plots (histograms, scatter, convergence, yield, correlation)

---

## Running the Demo

See [HOW_TO_RUN.md](HOW_TO_RUN.md) for full build, run, and visualization instructions.
