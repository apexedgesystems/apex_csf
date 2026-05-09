# ==============================================================================
# Module: sim_electronics_chips_intel4004_gate
#
# Intel 4004 gate-level extraction. Identifies the 427 NOR gates in the SPICE
# netlist by topology (drain shared with depletion load) and exposes their
# input/output net IDs. Used by the L1 component classifier in grid/.
#
# Libraries:
#   sim_electronics_chips_intel4004_gate - Gate extraction (header-only)
# ==============================================================================

include(apex/All)
apex_module(sim_electronics_chips_intel4004_gate)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

target_link_libraries(
  ${LIB_NAME}
  INTERFACE sim_electronics_chips_intel4004_grid sim_electronics_chips_intel4004_netlist
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst dtst ptst)
