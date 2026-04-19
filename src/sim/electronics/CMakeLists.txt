# ==============================================================================
# Module: sim_electronics
# Electronics Simulation
# ==============================================================================
#
# Circuit simulation framework with device models, MNA solvers, and time-stepping.
# Modular architecture with clean layer separation.
#
# Layers:
#   1. algorithms/   - Solvers (MNA, transient, nonlinear, spice/ngspice)
#   2. devices/      - Device physics (MOSFETs, resistors, capacitors, companions)
#   3. circuit/      - Circuit assembly API (general construction wrapper)
#
# Models (built on top of layers 1-3):
#   filters/         - Analog filter circuits (RC low-pass, ...)
#   gates/           - Digital logic gates and adders
#   intel4004/       - Intel 4004 microprocessor (behavioral + L1 transistor)
#
# ==============================================================================

# ------------------------------------------------------------------------------
# Infrastructure (Layers 1-3)
# ------------------------------------------------------------------------------

add_subdirectory(algorithms)
add_subdirectory(devices)
add_subdirectory(circuit)

# ------------------------------------------------------------------------------
# Models
# ------------------------------------------------------------------------------

add_subdirectory(amplifiers)
add_subdirectory(filters)
add_subdirectory(gates)
add_subdirectory(intel4004)
