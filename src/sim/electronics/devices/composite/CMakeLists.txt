# ==============================================================================
# Module: sim_electronics_devices_composite
#
# Composite device library (CMOS gates built from primitive transistors).
# Provides pre-built logic gates for digital circuit design.
#
# Libraries:
#   sim_electronics_devices_composite - Composite devices (header-only)
# ==============================================================================

include(apex/All)
apex_module(sim_electronics_devices_composite)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

target_link_libraries(
  ${LIB_NAME}
  INTERFACE sim_electronics_devices_descriptors sim_electronics_devices_nonlinear
            sim_electronics_algorithms_mna utilities_compatibility
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst)
