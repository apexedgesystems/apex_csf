# ==============================================================================
# Module: system_core_monte_carlo
#
# Header-only Monte Carlo batch execution infrastructure.
#
# Components:
#   MonteCarloDriver   - Thread pool orchestrator for independent simulation runs
#   MonteCarloResults  - Results container with scalar statistics
#   SweepGenerator     - Parameter variation utilities (grid, random, LHS)
#
# Design:
#   NOT an executive variant. Drives LiteExecutive instances (or raw model
#   calls) as workers. Parallelism is across runs, not within a single frame.
#   Each worker grabs runs from an atomic counter (work-stealing pattern).
#
# Libraries:
#   system_core_monte_carlo - Header-only interface library
# ==============================================================================

include(apex/All)
apex_module(system_core_monte_carlo)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(
  NAME
  ${LIB_NAME}
  INC
  "${INC_DIR}"
  DEPS_INTERFACE
  utilities_concurrency
  fmt::fmt
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst)
