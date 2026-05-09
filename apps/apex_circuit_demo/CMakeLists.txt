# ==============================================================================
# App: apex_circuit_demo
#
# General-purpose electronics circuit demonstration. Selects between
# transistor-level CMOS logic gates and analog filters via a single
# `--circuit` flag, exercising the apex Circuit + MNA + transient
# infrastructure without any Intel 4004-specific tooling.
#
# Demonstrates the composition pattern customers use to build their own
# circuit simulations: pick device models from `sim_electronics_devices_*`,
# describe a topology with `sim_electronics_circuit`, and analyse with
# `sim_electronics_algorithms_transient` (transient) or
# `MnaSystem::computeDC()` (DC operating point).
#
# Build:
#   make compose-debug
#
# Run:
#   ./build/native-linux-debug/bin/ApexCircuitDemo
#   ./build/native-linux-debug/bin/ApexCircuitDemo --circuit gates
#   ./build/native-linux-debug/bin/ApexCircuitDemo --circuit rc-lowpass --r 10e3 --c 1e-6
#   ./build/native-linux-debug/bin/ApexCircuitDemo --circuit rc-lowpass --vstep 3.3 --duration 5e-3
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
  ApexCircuitDemo
  SRC
  ${APP_SOURCES}
  LINK
  sim_electronics_topologies_gates
  sim_electronics_topologies_filters
  sim_electronics_topologies_amplifiers
  sim_electronics_circuit
  sim_electronics_algorithms_transient
  fmt::fmt
  REQUIRES
  POSIX
)
