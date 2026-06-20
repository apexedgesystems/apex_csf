# ==============================================================================
# Module: sim_electronics_topologies_filters
#
# Analog filter circuit models built using the general electronics framework.
# Header-only library demonstrating user-facing analog circuit construction.
#
# Libraries:
#   sim_electronics_topologies_filters - Filter circuit API (header-only)
# ==============================================================================

include(apex/All)
apex_module(sim_electronics_topologies_filters)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

target_link_libraries(
  ${LIB_NAME}
  INTERFACE sim_electronics_circuit sim_electronics_devices_linear
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs()
