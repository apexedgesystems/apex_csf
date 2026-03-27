#!/usr/bin/env python3
"""
mc_plot.py - Monte Carlo results visualization tool.

Reads per-run CSV output from ApexMcDemo and generates publication-quality
plots for tolerance analysis, yield visualization, and sensitivity studies.

Subcommands:
  histograms   - Distribution histograms with spec limits for each output field
  scatter      - Pairwise scatter plots showing correlation between outputs
  yield        - Yield vs threshold sweep (what-if analysis)
  convergence  - Running mean/stddev vs sample count (was N enough?)
  sensitivity  - Tornado chart showing which parameter drives which output
  report       - Generate all plots in one shot

Usage:
  mc-plot histograms mc_results.csv --output plots/
  mc-plot scatter mc_results.csv --x v_out --y ripple_mV
  mc-plot convergence mc_results.csv --field v_out
  mc-plot report mc_results.csv --output plots/

Dependencies:
  pandas, matplotlib, seaborn, numpy (all in pyproject.toml)
"""

import argparse
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import seaborn as sns

# =============================================================================
# Constants
# =============================================================================

DEFAULT_DPI = 150
FIGURE_WIDTH = 10
FIGURE_HEIGHT = 6
HIST_BINS = 80
COLORS = sns.color_palette("deep")

# Default spec limits for the regulator demo (field -> (limit, is_upper))
DEFAULT_SPECS: Dict[str, Tuple[float, bool, str]] = {
    "v_out": (3.3 * 1.03, True, "V_out upper spec (3.3V +3%)"),
    "ripple_mV": (50.0, True, "Ripple spec (50mV)"),
    "phase_margin": (45.0, False, "Phase margin spec (45 deg)"),
}

# Additional lower spec for v_out
VOUT_LOWER_SPEC = 3.3 * 0.97


# =============================================================================
# Plot Functions
# =============================================================================


def plot_histograms(
    df: pd.DataFrame,
    output_dir: Path,
    fields: Optional[List[str]] = None,
    specs: Optional[Dict] = None,
) -> None:
    """Generate distribution histograms with spec limits and statistics.

    Args:
        df: Per-run results DataFrame.
        output_dir: Directory for output PNG files.
        fields: Column names to plot (default: all numeric except run_index).
        specs: Spec limits dict {field: (limit, is_upper, label)}.
    """
    if specs is None:
        specs = DEFAULT_SPECS
    if fields is None:
        fields = [c for c in df.columns if c != "run_index" and c != "in_spec"]

    for field in fields:
        if field not in df.columns:
            continue

        fig, ax = plt.subplots(figsize=(FIGURE_WIDTH, FIGURE_HEIGHT))
        values = df[field].values

        # Histogram with KDE
        ax.hist(
            values,
            bins=HIST_BINS,
            density=True,
            alpha=0.7,
            color=COLORS[0],
            edgecolor="white",
            linewidth=0.5,
        )
        kde_x = np.linspace(values.min(), values.max(), 500)
        kde_bw = (values.max() - values.min()) / HIST_BINS
        if kde_bw > 0:
            from scipy.stats import gaussian_kde

            kde = gaussian_kde(values, bw_method=kde_bw / values.std())
            ax.plot(kde_x, kde(kde_x), color=COLORS[1], linewidth=2, label="KDE")

        # Spec limits
        if field in specs:
            limit, is_upper, label = specs[field]
            ax.axvline(limit, color="red", linestyle="--", linewidth=2, label=label)
            # Shade fail region
            if is_upper:
                ax.axvspan(limit, values.max() * 1.05, alpha=0.1, color="red")
            else:
                ax.axvspan(values.min() * 0.95, limit, alpha=0.1, color="red")

        # V_out gets both upper and lower spec lines
        if field == "v_out":
            ax.axvline(
                VOUT_LOWER_SPEC,
                color="red",
                linestyle="--",
                linewidth=2,
                label="V_out lower spec (3.3V -3%)",
            )

        # Statistics annotation
        mean = values.mean()
        std = values.std()
        p05, p95 = np.percentile(values, [5, 95])
        stats_text = (
            f"Mean: {mean:.6g}\n"
            f"Std:  {std:.6g}\n"
            f"P05:  {p05:.6g}\n"
            f"P95:  {p95:.6g}\n"
            f"N:    {len(values)}"
        )
        ax.text(
            0.98,
            0.95,
            stats_text,
            transform=ax.transAxes,
            verticalalignment="top",
            horizontalalignment="right",
            fontsize=9,
            fontfamily="monospace",
            bbox={"boxstyle": "round,pad=0.4", "facecolor": "wheat", "alpha": 0.8},
        )

        ax.set_xlabel(field, fontsize=12)
        ax.set_ylabel("Density", fontsize=12)
        ax.set_title(
            f"Monte Carlo Distribution: {field} (N={len(values)})", fontsize=13, fontweight="bold"
        )
        ax.legend(loc="upper left")
        ax.grid(True, alpha=0.3)

        fig.tight_layout()
        fig.savefig(output_dir / f"hist_{field}.png", dpi=DEFAULT_DPI)
        plt.close(fig)
        print(f"  Saved hist_{field}.png")


def plot_scatter(
    df: pd.DataFrame,
    output_dir: Path,
    x_field: str,
    y_field: str,
    color_field: Optional[str] = None,
) -> None:
    """Generate scatter plot between two output fields.

    Args:
        df: Per-run results DataFrame.
        output_dir: Directory for output PNG file.
        x_field: X-axis column name.
        y_field: Y-axis column name.
        color_field: Optional column for color coding (e.g., "in_spec").
    """
    fig, ax = plt.subplots(figsize=(FIGURE_WIDTH, FIGURE_HEIGHT))

    if color_field and color_field in df.columns:
        scatter = ax.scatter(
            df[x_field],
            df[y_field],
            c=df[color_field],
            cmap="RdYlGn",
            alpha=0.3,
            s=8,
            edgecolors="none",
        )
        cbar = plt.colorbar(scatter, ax=ax)
        cbar.set_label(color_field)
    else:
        ax.scatter(
            df[x_field],
            df[y_field],
            alpha=0.2,
            s=8,
            color=COLORS[0],
            edgecolors="none",
        )

    # Correlation coefficient
    corr = df[x_field].corr(df[y_field])
    ax.text(
        0.02,
        0.98,
        f"r = {corr:.4f}",
        transform=ax.transAxes,
        verticalalignment="top",
        fontsize=11,
        bbox={"boxstyle": "round,pad=0.3", "facecolor": "lightyellow", "alpha": 0.9},
    )

    ax.set_xlabel(x_field, fontsize=12)
    ax.set_ylabel(y_field, fontsize=12)
    ax.set_title(f"Scatter: {x_field} vs {y_field} (N={len(df)})", fontsize=13, fontweight="bold")
    ax.grid(True, alpha=0.3)

    fig.tight_layout()
    fname = f"scatter_{x_field}_vs_{y_field}.png"
    fig.savefig(output_dir / fname, dpi=DEFAULT_DPI)
    plt.close(fig)
    print(f"  Saved {fname}")


def plot_convergence(
    df: pd.DataFrame,
    output_dir: Path,
    fields: Optional[List[str]] = None,
) -> None:
    """Plot running mean and +/-1 sigma band vs sample count.

    Shows whether N runs was sufficient for stable statistics.

    Args:
        df: Per-run results DataFrame.
        output_dir: Directory for output PNG files.
        fields: Column names to plot.
    """
    if fields is None:
        fields = [c for c in df.columns if c != "run_index" and c != "in_spec"]

    for field in fields:
        if field not in df.columns:
            continue

        values = df[field].values
        n = len(values)

        # Compute running statistics
        sample_points = np.unique(np.geomspace(10, n, num=500, dtype=int))
        running_mean = np.zeros(len(sample_points))
        running_sem = np.zeros(len(sample_points))

        for i, k in enumerate(sample_points):
            subset = values[:k]
            running_mean[i] = subset.mean()
            running_sem[i] = subset.std() / np.sqrt(k) if k > 1 else 0

        fig, ax = plt.subplots(figsize=(FIGURE_WIDTH, FIGURE_HEIGHT))

        ax.plot(sample_points, running_mean, color=COLORS[0], linewidth=2, label="Running mean")
        ax.fill_between(
            sample_points,
            running_mean - 2 * running_sem,
            running_mean + 2 * running_sem,
            alpha=0.2,
            color=COLORS[0],
            label="+/- 2 SEM",
        )

        # Final mean reference line
        ax.axhline(
            values.mean(),
            color="gray",
            linestyle=":",
            linewidth=1,
            label=f"Final mean: {values.mean():.6g}",
        )

        ax.set_xlabel("Sample Count", fontsize=12)
        ax.set_ylabel(field, fontsize=12)
        ax.set_title(f"Convergence: {field}", fontsize=13, fontweight="bold")
        ax.set_xscale("log")
        ax.legend()
        ax.grid(True, alpha=0.3)

        fig.tight_layout()
        fig.savefig(output_dir / f"convergence_{field}.png", dpi=DEFAULT_DPI)
        plt.close(fig)
        print(f"  Saved convergence_{field}.png")


def plot_yield_sweep(
    df: pd.DataFrame,
    output_dir: Path,
    field: str,
    is_upper: bool = True,
    num_points: int = 200,
) -> None:
    """Plot yield (pass rate) as a function of spec limit.

    Answers: "If I tighten/loosen the spec, how does yield change?"

    Args:
        df: Per-run results DataFrame.
        output_dir: Directory for output PNG file.
        field: Column name to sweep.
        is_upper: True for upper limit (pass = value <= limit).
        num_points: Number of threshold points to evaluate.
    """
    values = df[field].values
    thresholds = np.linspace(values.min(), values.max(), num_points)

    yields = np.zeros(num_points)
    for i, thresh in enumerate(thresholds):
        if is_upper:
            yields[i] = np.mean(values <= thresh) * 100.0
        else:
            yields[i] = np.mean(values >= thresh) * 100.0

    fig, ax = plt.subplots(figsize=(FIGURE_WIDTH, FIGURE_HEIGHT))
    ax.plot(thresholds, yields, color=COLORS[0], linewidth=2)

    # Mark common yield targets
    for target in [90, 95, 99]:
        idx = np.argmin(np.abs(yields - target))
        ax.axhline(target, color="gray", linestyle=":", alpha=0.5)
        ax.plot(thresholds[idx], target, "o", color=COLORS[2], markersize=8)
        ax.annotate(
            f"{target}% @ {thresholds[idx]:.4g}",
            xy=(thresholds[idx], target),
            xytext=(10, 10),
            textcoords="offset points",
            fontsize=9,
            fontweight="bold",
        )

    direction = "<=" if is_upper else ">="
    ax.set_xlabel(f"{field} spec limit", fontsize=12)
    ax.set_ylabel("Yield (%)", fontsize=12)
    ax.set_title(f"Yield Sweep: {field} (pass = {direction} limit)", fontsize=13, fontweight="bold")
    ax.set_ylim(0, 105)
    ax.grid(True, alpha=0.3)

    fig.tight_layout()
    fig.savefig(output_dir / f"yield_{field}.png", dpi=DEFAULT_DPI)
    plt.close(fig)
    print(f"  Saved yield_{field}.png")


def plot_correlation_matrix(
    df: pd.DataFrame,
    output_dir: Path,
    fields: Optional[List[str]] = None,
) -> None:
    """Plot correlation heatmap between all output fields.

    Args:
        df: Per-run results DataFrame.
        output_dir: Directory for output PNG file.
        fields: Column names to include.
    """
    if fields is None:
        fields = [c for c in df.columns if c != "run_index"]

    corr = df[fields].corr()

    fig, ax = plt.subplots(figsize=(8, 6))
    sns.heatmap(
        corr,
        annot=True,
        fmt=".3f",
        cmap="RdBu_r",
        center=0,
        vmin=-1,
        vmax=1,
        ax=ax,
        square=True,
        cbar_kws={"label": "Pearson r"},
    )
    ax.set_title("Output Correlation Matrix", fontsize=13, fontweight="bold")

    fig.tight_layout()
    fig.savefig(output_dir / "correlation_matrix.png", dpi=DEFAULT_DPI)
    plt.close(fig)
    print("  Saved correlation_matrix.png")


# =============================================================================
# Report (all plots)
# =============================================================================


def generate_report(df: pd.DataFrame, output_dir: Path) -> None:
    """Generate all plots for a complete MC analysis report.

    Args:
        df: Per-run results DataFrame.
        output_dir: Directory for output PNG files.
    """
    output_dir.mkdir(parents=True, exist_ok=True)

    numeric_fields = [c for c in df.columns if c != "run_index" and c != "in_spec"]

    print("Generating histograms...")
    plot_histograms(df, output_dir)

    print("Generating scatter plots...")
    # Key pairwise correlations
    pairs = [
        ("v_out", "ripple_mV"),
        ("v_out", "settling_us"),
        ("ripple_mV", "settling_us"),
        ("ripple_mV", "phase_margin"),
    ]
    for x, y in pairs:
        if x in df.columns and y in df.columns:
            plot_scatter(df, output_dir, x, y, color_field="in_spec")

    print("Generating convergence plots...")
    plot_convergence(df, output_dir, fields=numeric_fields[:3])

    print("Generating yield sweeps...")
    if "ripple_mV" in df.columns:
        plot_yield_sweep(df, output_dir, "ripple_mV", is_upper=True)
    if "v_out" in df.columns:
        plot_yield_sweep(df, output_dir, "v_out", is_upper=True)

    print("Generating correlation matrix...")
    plot_correlation_matrix(df, output_dir, fields=numeric_fields)

    print(f"\nReport complete: {len(list(output_dir.glob('*.png')))} plots in {output_dir}/")


# =============================================================================
# CLI
# =============================================================================


def main() -> None:
    """CLI entry point."""
    parser = argparse.ArgumentParser(
        prog="mc-plot",
        description="Monte Carlo results visualization tool",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    # -- histograms --
    hist_p = subparsers.add_parser("histograms", help="Distribution histograms")
    hist_p.add_argument("csv", type=Path, help="Per-run results CSV")
    hist_p.add_argument("--output", type=Path, default=Path("."), help="Output directory")
    hist_p.add_argument("--fields", nargs="+", help="Fields to plot")

    # -- scatter --
    scat_p = subparsers.add_parser("scatter", help="Pairwise scatter plot")
    scat_p.add_argument("csv", type=Path, help="Per-run results CSV")
    scat_p.add_argument("--x", required=True, help="X-axis field")
    scat_p.add_argument("--y", required=True, help="Y-axis field")
    scat_p.add_argument("--color", help="Color-code field")
    scat_p.add_argument("--output", type=Path, default=Path("."), help="Output directory")

    # -- convergence --
    conv_p = subparsers.add_parser("convergence", help="Running statistics vs sample count")
    conv_p.add_argument("csv", type=Path, help="Per-run results CSV")
    conv_p.add_argument("--fields", nargs="+", help="Fields to plot")
    conv_p.add_argument("--output", type=Path, default=Path("."), help="Output directory")

    # -- yield --
    yield_p = subparsers.add_parser("yield", help="Yield vs threshold sweep")
    yield_p.add_argument("csv", type=Path, help="Per-run results CSV")
    yield_p.add_argument("--field", required=True, help="Field to sweep")
    yield_p.add_argument("--lower", action="store_true", help="Lower limit (pass = value >= limit)")
    yield_p.add_argument("--output", type=Path, default=Path("."), help="Output directory")

    # -- report --
    rep_p = subparsers.add_parser("report", help="Generate all plots")
    rep_p.add_argument("csv", type=Path, help="Per-run results CSV")
    rep_p.add_argument("--output", type=Path, default=Path("mc_report"), help="Output directory")

    args = parser.parse_args()

    # Load CSV
    df = pd.read_csv(args.csv)
    print(f"Loaded {len(df)} runs, {len(df.columns)} columns from {args.csv}")

    if args.command == "histograms":
        args.output.mkdir(parents=True, exist_ok=True)
        plot_histograms(df, args.output, fields=args.fields)

    elif args.command == "scatter":
        args.output.mkdir(parents=True, exist_ok=True)
        plot_scatter(df, args.output, args.x, args.y, color_field=args.color)

    elif args.command == "convergence":
        args.output.mkdir(parents=True, exist_ok=True)
        plot_convergence(df, args.output, fields=args.fields)

    elif args.command == "yield":
        args.output.mkdir(parents=True, exist_ok=True)
        plot_yield_sweep(df, args.output, args.field, is_upper=not args.lower)

    elif args.command == "report":
        generate_report(df, args.output)


if __name__ == "__main__":
    main()
