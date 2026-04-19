# ==============================================================================
# Module: sim_electronics_intel4004_netlist
#
# SPICE netlist parser for the Intel 4004. Reads the verified 2,242-transistor
# netlist from Kintli/Silverman reverse engineering (lajos-4004.spice).
#
# Libraries:
#   sim_electronics_intel4004_netlist - Netlist parser (header-only)
# ==============================================================================

include(apex/All)
apex_module(sim_electronics_intel4004_netlist)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst)
