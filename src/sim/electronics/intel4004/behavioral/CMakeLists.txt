# ==============================================================================
# Module: sim_electronics_intel4004_behavioral
#
# Behavioral model of the Intel 4004 CPU -- 46 instructions, 4-bit data path,
# 12-bit address space. Golden reference for verifying the transistor-level
# circuit simulation.
#
# Libraries:
#   sim_electronics_intel4004_behavioral - Behavioral CPU model (header-only)
# ==============================================================================

include(apex/All)
apex_module(sim_electronics_intel4004_behavioral)

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
