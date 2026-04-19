# ==============================================================================
# Module: sim_electronics_intel4004_grid
#
# Intel 4004 transistor-level circuit assembly. Converts the parsed SPICE netlist
# (2,242 PMOS transistors, ~1,081 nets) into a Circuit object for MNA simulation.
#
# Libraries:
#   sim_electronics_intel4004_grid - Circuit assembly (header-only)
# ==============================================================================

include(apex/All)
apex_module(sim_electronics_intel4004_grid)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

target_link_libraries(
  ${LIB_NAME}
  INTERFACE sim_electronics_circuit sim_electronics_intel4004_netlist
            sim_electronics_intel4004_behavioral sim_electronics_devices_composite
            sim_electronics_devices_nonlinear
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst dtst)
