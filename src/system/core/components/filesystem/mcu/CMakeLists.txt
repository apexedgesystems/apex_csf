# ==============================================================================
# filesystem_mcu - No-op filesystem for MCU targets
#
# Header-only library providing NullFileSystem for resource-constrained systems.
# All operations are no-ops that succeed without doing anything.
#
# Components:
#   - NullFileSystem - No-op IComponent implementation
# ==============================================================================

include(apex/All)
apex_module(filesystem_mcu)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(
  NAME
  ${LIB_NAME}
  INC
  "${INC_DIR}"
  DEPS_INTERFACE
  filesystem_base
  system_component_base
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst)
