# ==============================================================================
# App: apex_mc_demo
#
# Monte Carlo voltage regulator tolerance analysis demo.
# Sweeps component tolerances across all CPU cores and reports yield,
# statistics, convergence, and sensitivity.
#
# Build:
#   make compose-debug
#
# Run:
#   ./build/native-linux-debug/bin/ApexMcDemo
#   ./build/native-linux-debug/bin/ApexMcDemo --runs 100000 --csv results.csv
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

# ------------------------------------------------------------------------------
# Executable
# ------------------------------------------------------------------------------

apex_add_app(
  NAME
  ApexMcDemo
  SRC
  ${APP_SOURCES}
  LINK
  system_core_monte_carlo
  sim_analog_regulator
  fmt::fmt
  REQUIRES
  POSIX
)
