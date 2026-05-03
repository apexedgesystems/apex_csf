# ==============================================================================
# Module: hal_linux
#
# Linux-specific HAL implementations. Targets hosted POSIX systems
# (Ubuntu, embedded Linux distributions, Jetson, RPi, RISC-V SoCs).
# Bare-metal builds skip this directory entirely.
#
# Libraries:
#   hal_linux - Linux HAL implementations (INTERFACE, header-only)
# ==============================================================================

# Skip on bare-metal targets entirely.
if (APEX_PLATFORM_BAREMETAL)
  return()
endif ()

include(apex/All)

# ------------------------------------------------------------------------------
# Interface Library (header-only)
# ------------------------------------------------------------------------------

apex_add_interface_library(
  NAME
  hal_linux
  INC
  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  DEPS_INTERFACE
  hal_interface
)

# Additional include for src/ path resolution
target_include_directories(hal_linux INTERFACE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(hal_linux)

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/utst/CMakeLists.txt")
  add_subdirectory(utst)
endif ()
