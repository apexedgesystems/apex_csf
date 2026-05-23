# ==============================================================================
# Module: sim_electronics_devices
#
# Device model library (Layer 2). Provides physics engines for circuit simulation.
#
# Subdirectories:
#   descriptors/ - Component topology (NetIDs, parameters)
#   linear/      - Linear device models (R, C, L)
#   nonlinear/   - Nonlinear device models (diode, MOSFET, BJT)
#   composite/   - High-level circuit fragments (CMOS gates)
#
# Companion models for transient simulation (C, L integration) live under
# algorithms/companions/ -- they're a numerical method, not device physics.
# ==============================================================================

add_subdirectory(composite)
add_subdirectory(descriptors)
add_subdirectory(linear)
add_subdirectory(nonlinear)
