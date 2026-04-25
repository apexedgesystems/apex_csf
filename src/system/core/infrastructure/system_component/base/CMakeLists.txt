# ==============================================================================
# system_component_base - Pure interface library (header-only)
#
# Provides the minimal IComponent interface and common types that all component
# implementations must satisfy. No heavy dependencies - suitable for bare-metal.
#
# Dependents:
#   - system_component_apex (SystemComponentBase hierarchy)
#   - system_component_mcu (McuComponentBase) [future]
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME system_component_base INC "${PROJECT_SOURCE_DIR}" BAREMETAL)

# No source files - header-only interface library

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen)
# ------------------------------------------------------------------------------

apex_standard_optins(system_component_base)

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/utst/CMakeLists.txt")
  add_subdirectory(utst)
endif ()
