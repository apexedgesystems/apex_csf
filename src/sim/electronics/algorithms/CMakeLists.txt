# ==============================================================================
# Module: sim_electronics_algorithms
# ==============================================================================
#
# Layer 1: Algorithm libraries for electronics simulation.
#
# Mathematical solvers that power circuit simulation. Domain-independent and
# topology-independent.
#
# Libraries:
#   - mna/        - Modified Nodal Analysis (DC, AC, sparse/dense solvers)
#   - transient/  - Time-domain integration (backward Euler, trapezoidal, GEAR2)
#   - nonlinear/  - Newton-Raphson nonlinear solver for diodes, transistors
#   - spice/      - SPICE integration (ngspice wrapper for verification)
#
# ==============================================================================

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

add_subdirectory(mna)
add_subdirectory(transient)
add_subdirectory(nonlinear)
add_subdirectory(spice)
