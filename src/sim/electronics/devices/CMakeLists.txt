# ==============================================================================
# Module: sim_electronics_devices
#
# Device model library (Layer 2). Provides physics engines for circuit simulation.
#
# Subdirectories:
#   companions/  - Companion models for transient simulation (C, L integration)
#   descriptors/ - Component topology (NetIDs, parameters)
#   linear/      - Linear device models (R, C, L)
#   nonlinear/   - Nonlinear device models (diode, MOSFET, BJT)
#   composite/   - High-level circuit fragments (CMOS gates)
# ==============================================================================

add_subdirectory(companions)
add_subdirectory(composite)
add_subdirectory(descriptors)
add_subdirectory(linear)
add_subdirectory(nonlinear)
