# ==============================================================================
# Module: sim_electronics
# Electronics Simulation
# ==============================================================================
#
# Circuit simulation framework with device models, MNA solvers, and time-stepping.
# Modular architecture with clean layer separation.
#
# Layers:
#   1. algorithms/   - Solvers (MNA, transient, nonlinear, companions, spice)
#   2. devices/      - Device physics (R/L/C, diodes, MOSFETs, BJTs, JFETs)
#   3. circuit/      - Circuit assembly API (general construction wrapper)
#
# Topologies (pre-built circuits, built on top of layers 1-3):
#   topologies/gates/      - CMOS logic gates (boolean + transistor)
#   topologies/filters/    - Analog filter circuits (RC low-pass, ...)
#   topologies/amplifiers/ - Amplifier topologies (BJT common-emitter, ...)
#
# Chip-scale assemblies (each chip is its own multi-fidelity stack):
#   chips/intel4004/       - Intel 4004 microprocessor (L0 / Lg / L1 / L2)
#
# ==============================================================================

# ------------------------------------------------------------------------------
# Infrastructure (Layers 1-3)
# ------------------------------------------------------------------------------

add_subdirectory(algorithms)
add_subdirectory(devices)
add_subdirectory(circuit)

# ------------------------------------------------------------------------------
# Topologies + chip-scale assemblies
# ------------------------------------------------------------------------------

add_subdirectory(topologies)
add_subdirectory(chips)
