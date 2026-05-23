# ==============================================================================
# Module: sim_electronics_devices_linear
#
# Linear device models (resistor, capacitor, inductor). Provides physics
# equations and stamping functions for exact linear circuit elements.
#
# Linear devices are exact (no approximations, no iterations) with one
# fidelity level. Capacitors and inductors use companion models from the
# transient library for time-domain simulation.
#
# Libraries:
#   sim_electronics_devices_linear  - Linear device models (header-only)
# ==============================================================================

include(apex/All)
apex_module(sim_electronics_devices_linear)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

target_link_libraries(
  ${LIB_NAME}
  INTERFACE
    sim_electronics_algorithms_mna # MNA stamping (MnaSystem, MnaSystemSparse)
    sim_electronics_algorithms_companions # Companion models (CapacitorCompanion, InductorCompanion)
    utilities_compatibility # C++17/20/23 compatibility
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst)
