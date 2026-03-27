# ==============================================================================
# hal_interface - Platform-agnostic HAL interfaces (header-only)
#
# Provides abstract interfaces (IUart, ICan, ISpi, II2c, IFlash) with no
# platform dependencies. Suitable for bare-metal targets.
#
# Libraries:
#   hal_interface - Abstract interfaces (INTERFACE, header-only)
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME hal_interface INC "${CMAKE_CURRENT_SOURCE_DIR}" BAREMETAL)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(hal_interface)

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/utst/CMakeLists.txt")
  add_subdirectory(utst)
endif ()
