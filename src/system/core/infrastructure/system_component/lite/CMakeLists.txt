# ==============================================================================
# system_component_lite - Minimal component base for MCU targets
#
# Header-only library providing LiteComponentBase for resource-constrained
# systems. No heap allocation, no std::filesystem dependencies.
#
# Components:
#   - LiteComponentBase - Minimal IComponent implementation
# ==============================================================================

include(apex/All)
apex_module(system_component_lite)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}" DEPS_INTERFACE system_component_base)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst)
