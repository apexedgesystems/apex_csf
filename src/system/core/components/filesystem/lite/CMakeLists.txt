# ==============================================================================
# filesystem_lite - No-op filesystem for MCU targets
#
# Header-only library providing NullFileSystem for resource-constrained systems.
# All operations are no-ops that succeed without doing anything.
#
# Components:
#   - NullFileSystem - No-op IComponent implementation
# ==============================================================================

include(apex/All)
apex_module(filesystem_lite)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}" DEPS_INTERFACE system_component_base)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst)
