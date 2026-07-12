# Monte Carlo Demo

Monte Carlo voltage-regulator tolerance analysis. Sweeps component
tolerances across all available CPU cores and reports yield, summary
statistics, convergence behavior, and per-parameter sensitivity.

## Building

```bash
make compose-debug
```

## Running

```bash
./build/hosted-x86_64-debug/bin/ApexMcDemo
./build/hosted-x86_64-debug/bin/ApexMcDemo --runs 100000 --csv results.csv
```

## See Also

- [docs/HOW_TO_RUN.md](docs/HOW_TO_RUN.md) -- step-by-step run guide.
- [docs/RESULTS.md](docs/RESULTS.md) -- expected yield curves and
  reference plots.
