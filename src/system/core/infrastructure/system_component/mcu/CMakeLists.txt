# ==============================================================================
# system_component_mcu - Minimal component base for MCU targets
#
# Header-only library providing McuComponentBase for resource-constrained
# systems. No heap allocation, no std::filesystem dependencies.
#
# Components:
#   - McuComponentBase - Minimal IComponent implementation
# ==============================================================================

include(apex/All)
apex_module(system_component_mcu)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}" DEPS_INTERFACE system_component_base
                           system_component_core
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst)
