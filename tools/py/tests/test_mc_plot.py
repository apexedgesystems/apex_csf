"""Unit tests for apex_tools.mc.mc_plot module.

Tests plot generation functions with synthetic dataframes. Verifies that
functions run without error and produce expected output files.
"""

import numpy as np
import pandas as pd
from apex_tools.mc.mc_plot import (
    plot_convergence,
    plot_correlation_matrix,
    plot_histograms,
    plot_scatter,
    plot_yield_sweep,
)


def _sample_df(n: int = 500) -> pd.DataFrame:
    """Create a synthetic MC results dataframe for testing."""
    rng = np.random.default_rng(42)
    return pd.DataFrame(
        {
            "run_index": range(n),
            "v_out": rng.normal(3.3, 0.025, n),
            "ripple_mV": rng.lognormal(3.5, 0.3, n),
            "settling_us": rng.normal(400, 80, n),
            "phase_margin": rng.normal(94, 1.0, n),
            "in_spec": rng.integers(0, 2, n),
        }
    )


# =============================== Histograms ================================


def test_plot_histograms_creates_files(tmp_path):
    """plot_histograms creates PNG files in the output directory."""
    df = _sample_df()
    plot_histograms(df, tmp_path)
    pngs = list(tmp_path.glob("hist_*.png"))
    assert len(pngs) > 0


# =============================== Scatter ================================


def test_plot_scatter_creates_file(tmp_path):
    """plot_scatter creates a scatter plot PNG."""
    df = _sample_df()
    plot_scatter(df, tmp_path, "v_out", "ripple_mV")
    pngs = list(tmp_path.glob("scatter_*.png"))
    assert len(pngs) == 1


# =============================== Convergence ================================


def test_plot_convergence_creates_files(tmp_path):
    """plot_convergence creates convergence PNG files."""
    df = _sample_df()
    plot_convergence(df, tmp_path, ["v_out", "ripple_mV"])
    pngs = list(tmp_path.glob("convergence_*.png"))
    assert len(pngs) == 2


# =============================== Yield Sweep ================================


def test_plot_yield_sweep_creates_file(tmp_path):
    """plot_yield_sweep creates a yield PNG."""
    df = _sample_df()
    plot_yield_sweep(df, tmp_path, "ripple_mV")
    pngs = list(tmp_path.glob("yield_*.png"))
    assert len(pngs) == 1


# =============================== Correlation ================================


def test_plot_correlation_matrix_creates_file(tmp_path):
    """plot_correlation_matrix creates a correlation heatmap PNG."""
    df = _sample_df()
    plot_correlation_matrix(df, tmp_path)
    pngs = list(tmp_path.glob("correlation_*.png"))
    assert len(pngs) == 1
