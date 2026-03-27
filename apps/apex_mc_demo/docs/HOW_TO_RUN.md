# How to Run: Monte Carlo Demo

Step-by-step commands to build, run, and visualize the LDO voltage regulator
Monte Carlo tolerance analysis.

---

## 1. Build

```bash
# Build the C++ demo app (native debug)
make compose-debug
```

The executable is at `build/native-linux-debug/bin/ApexMcDemo`.

---

## 2. Run the Demo

### Quick run (defaults: 10K runs, seed=42)

```bash
docker compose run --rm -T dev-cuda bash -c '
  ./build/native-linux-debug/bin/ApexMcDemo
'
```

### Full run with CSV export

```bash
docker compose run --rm -T dev-cuda bash -c '
  ./build/native-linux-debug/bin/ApexMcDemo \
    --runs 50000 \
    --seed 42 \
    --csv apps/apex_mc_demo/results/run_1/results.csv \
    --summary apps/apex_mc_demo/results/run_1/summary.csv
'
```

### CLI flags

| Flag             | Default | Description                         |
| ---------------- | ------- | ----------------------------------- |
| `--runs N`       | 10000   | Number of MC iterations             |
| `--threads N`    | auto    | Worker thread count (0 = all cores) |
| `--seed N`       | 42      | RNG seed (deterministic per seed)   |
| `--csv PATH`     | (none)  | Export per-run results CSV          |
| `--summary PATH` | (none)  | Export summary statistics CSV       |
| `-h, --help`     |         | Show help                           |

---

## 3. Build the Plotting Tool

```bash
# Build Python tools (poetry wheel + pip install to build dir)
make compose-tools-py
```

This installs `mc-plot` to `build/native-linux-debug/bin/tools/py/`.

---

## 4. Generate Plots

```bash
docker compose run --rm -T dev-cuda bash -c '
  cd build/native-linux-debug
  source .env

  # Full report (all 14 plots)
  mc-plot report \
    ../../apps/apex_mc_demo/results/run_1/results.csv \
    --output ../../apps/apex_mc_demo/results/run_1/
'
```

### Individual plot types

```bash
# Histograms with spec limits
mc-plot histograms results.csv --output plots/

# Scatter plot (two fields, color-coded by in_spec)
mc-plot scatter results.csv --x v_out --y ripple_mV --color in_spec --output plots/

# Convergence (running mean vs sample count)
mc-plot convergence results.csv --fields v_out ripple_mV --output plots/

# Yield sweep (what-if on spec limit)
mc-plot yield results.csv --field ripple_mV --output plots/

# Yield sweep with lower limit
mc-plot yield results.csv --field phase_margin --lower --output plots/
```

---

## 5. Reproduce the Archived Results

The three archived runs are stored as tarballs in `docs/results/`. To inspect:

```bash
cd apps/apex_mc_demo/docs/results
tar xzf run_1.tar.gz   # Extracts run_1/ with CSVs and PNGs
```

They were generated with:

```bash
BIN=./build/native-linux-debug/bin/ApexMcDemo
RESULTS=apps/apex_mc_demo/results

# Run 1: baseline (seed=42, 50K runs)
$BIN --runs 50000 --seed 42 \
  --csv $RESULTS/run_1/results.csv \
  --summary $RESULTS/run_1/summary.csv

# Run 2: reproducibility check (seed=1337, 50K runs)
$BIN --runs 50000 --seed 1337 \
  --csv $RESULTS/run_2/results.csv \
  --summary $RESULTS/run_2/summary.csv

# Run 3: convergence scaling (seed=99999, 100K runs)
$BIN --runs 100000 --seed 99999 \
  --csv $RESULTS/run_3/results.csv \
  --summary $RESULTS/run_3/summary.csv
```

Then for each run:

```bash
mc-plot report $RESULTS/run_N/results.csv --output $RESULTS/run_N/
```

---

## 6. Run Unit Tests

```bash
# Monte Carlo infrastructure (72 tests)
make compose-testp

# Or target specific test suites
docker compose run --rm -T dev-cuda bash -c '
  ./build/native-linux-debug/bin/tests/TestSystemCoreMonteCarlo
  ./build/native-linux-debug/bin/tests/TestSimAnalogRegulator
'
```

---

## Output Files

### CSV columns (results.csv)

| Column         | Unit | Description                        |
| -------------- | ---- | ---------------------------------- |
| `run_index`    | -    | Zero-based run index               |
| `v_out`        | V    | Regulated output voltage           |
| `ripple_mV`    | mV   | Peak ripple from load transient    |
| `settling_us`  | us   | Settling time to 1% of final value |
| `phase_margin` | deg  | Phase margin estimate              |
| `in_spec`      | 0/1  | 1 if V_out within +/-3% of 3.3V    |

### Summary CSV columns (summary.csv)

| Column                               | Description                 |
| ------------------------------------ | --------------------------- |
| `field`                              | Output field name           |
| `count`                              | Number of runs              |
| `mean`, `stddev`                     | Central tendency and spread |
| `min`, `max`                         | Range                       |
| `p05`, `p25`, `median`, `p75`, `p95` | Percentile distribution     |

### Plot files (14 per report)

| Plot                         | What it shows                                   |
| ---------------------------- | ----------------------------------------------- |
| `hist_*.png` (4)             | Distribution histogram with spec limits and KDE |
| `scatter_*_vs_*.png` (4)     | Pairwise output correlation with color coding   |
| `convergence_*.png` (3)      | Running mean +/- 2 SEM vs sample count          |
| `yield_*.png` (2)            | Yield % vs spec limit threshold                 |
| `correlation_matrix.png` (1) | Pearson r heatmap across all outputs            |

---

## Troubleshooting

| Problem                      | Fix                                                                     |
| ---------------------------- | ----------------------------------------------------------------------- |
| `mc-plot: command not found` | Run `make compose-tools-py` then `source build/native-linux-debug/.env` |
| `No module named matplotlib` | Python tools not built. Run `make compose-tools-py`                     |
| CSV is empty                 | Check `--csv` path is writable                                          |
| All yields 100%              | Tolerances may be too tight. Check model parameters                     |
| Results differ from archived | Verify `--seed` matches. Same seed = identical results                  |
