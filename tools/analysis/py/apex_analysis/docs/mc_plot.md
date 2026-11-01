# mc-plot

Monte Carlo result visualization: histograms, scatter plots, convergence analysis, and yield sweeps.

---

## Overview

`mc-plot` reads Monte Carlo simulation CSV output and generates analysis plots.
Input CSV must have a header row; numeric columns are auto-detected. Columns
named `spec_lo` or `spec_hi` are used as spec limit overlays in histogram plots.

---

## Commands

### histograms

Distribution histograms with KDE overlay and optional spec limits.

```
mc-plot histograms <csv> [--fields <f1> <f2> ...] [--output <dir>]
```

| Option           | Description                                                  |
| ---------------- | ------------------------------------------------------------ |
| `--fields`       | Fields to plot (default: all numeric columns).               |
| `--output <dir>` | Output directory for PNG files (default: current directory). |

### scatter

Pairwise scatter plot with Pearson correlation coefficient.

```
mc-plot scatter <csv> --x <field> --y <field> [--color <field>] [--output <dir>]
```

| Option            | Description                   |
| ----------------- | ----------------------------- |
| `--x <field>`     | X-axis field (required).      |
| `--y <field>`     | Y-axis field (required).      |
| `--color <field>` | Field to use for point color. |
| `--output <dir>`  | Output directory.             |

### convergence

Running mean and standard deviation vs. sample count.

```
mc-plot convergence <csv> [--fields <f1> <f2> ...] [--output <dir>]
```

### yield

Yield rate vs. spec limit sweep (what-if analysis).

```
mc-plot yield <csv> --field <field> [--lower] [--output <dir>]
```

| Option            | Description                                         |
| ----------------- | --------------------------------------------------- |
| `--field <field>` | Field to analyze (required).                        |
| `--lower`         | Treat spec as a lower bound (default: upper bound). |
| `--output <dir>`  | Output directory.                                   |

### report

Generate all plots for a complete analysis package.

```
mc-plot report <csv> [--output <dir>]
```

---

## Examples

```bash
# Generate all plots from a Monte Carlo run
mc-plot report results.csv --output plots/

# Distribution histograms for specific fields
mc-plot histograms results.csv --fields alt_error vel_error --output plots/

# Scatter with color-coded third variable
mc-plot scatter results.csv --x alt_error --y vel_error --color wind_speed

# Yield vs upper spec limit for altitude error
mc-plot yield results.csv --field alt_error --output plots/

# Convergence check (did we run enough samples?)
mc-plot convergence results.csv --fields alt_error --output plots/
```
