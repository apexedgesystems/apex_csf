# ==============================================================================
# Module: sim_electronics_devices_descriptors
#
# Device topology descriptors (pure topology, no physics). Specify circuit
# connectivity (NetIDs) and device parameters, independent of simulation models.
#
# Descriptors enable:
# - Netlist parsing separate from physics model selection
# - Fidelity switching (same topology, different physics accuracy)
# - Test isolation (verify connectivity without running simulation)
#
# Libraries:
#   sim_electronics_devices_descriptors  - Topology descriptors (header-only)
# ==============================================================================

include(apex/All)
apex_module(sim_electronics_devices_descriptors)

# ------------------------------------------------------------------------------
# Interface Library (Header-Only)
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

target_link_libraries(
  ${LIB_NAME}
  INTERFACE sim_electronics_mna # NetID type
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst)
