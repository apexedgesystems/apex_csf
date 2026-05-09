# ==============================================================================
# Module: sim_electronics_topologies_gates
#
# User-facing logic gate API wrapping the existing CMOS composite gates.
# Provides truth tables (LogicGates.hpp) and circuit-level CMOS gate models
# (CmosGateCircuits.hpp) for all 7 standard gate types (NOT, NAND, NOR, AND,
# OR, XOR, XNOR) solved through the MNA infrastructure with MosfetLevel1
# physics.
#
# Libraries:
#   sim_electronics_topologies_gates - Logic gates API (header-only)
# ==============================================================================

include(apex/All)
apex_module(sim_electronics_topologies_gates)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

target_link_libraries(
  ${LIB_NAME}
  INTERFACE sim_electronics_devices_composite
            sim_electronics_circuit
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst dtst)
