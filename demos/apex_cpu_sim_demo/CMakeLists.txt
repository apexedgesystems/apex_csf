# ==============================================================================
# App: apex_cpu_sim_demo
#
# Intel 4004 CPU Simulator demo at three production fidelity levels:
#   L0: Behavioral CPU (fast functional model)
#   L1: Component hybrid (1305 NOR gates at Level 1 Shichman-Hodges,
#       222 pass gates and 610 dynamic storage at binary switch,
#       105 standalone loads as resistive G_LOAD, behavioral timing
#       injection)
#   L2: Engineered physics (BSIM3 smooth Vgst_eff on the 338 cross-coupled
#       latch transistors, Meyer intrinsic + overlap caps on every device,
#       66 layout-extracted bootstrap caps loaded from a data file,
#       behavioral overlay OFF)
#
# Multi-byte execution uses the L0/L1/L2 hybrid: L0 is authoritative for
# instruction state and a fresh transistor circuit is built per byte
# (seeded from L0's prior ACC) for transistor-level visibility.
#
# Build:
#   make compose-debug
#
# Run:
#   ./build/native-linux-debug/bin/ApexCpuSimDemo                 # L0+L1 default
#   ./build/native-linux-debug/bin/ApexCpuSimDemo --level 0       # L0 only
#   ./build/native-linux-debug/bin/ApexCpuSimDemo --level 2       # L0+L2 engineered
#   ./build/native-linux-debug/bin/ApexCpuSimDemo --program "D5 00 D3"
#   ./build/native-linux-debug/bin/ApexCpuSimDemo --probe ACC.0 --probe D0
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
  ApexCpuSimDemo
  SRC
  ${APP_SOURCES}
  LINK
  sim_electronics_chips_intel4004_grid
  sim_electronics_chips_intel4004_netlist
  sim_electronics_devices_nonlinear
  sim_electronics_algorithms_mna
  fmt::fmt
  REQUIRES
  POSIX
)
