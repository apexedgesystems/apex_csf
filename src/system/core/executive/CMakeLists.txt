# ==============================================================================
# executive - Executive implementations for system control
#
# Directory structure:
#   base/  - IExecutive interface (header-only, no heavy deps)
#   core/  - ExecutiveCore shared concrete base (header-only, no heavy deps)
#   posix/ - PosixExecutiveBase / ApexExecutive (POSIX tier)
#   mcu/   - McuExecutive (MCU tier)
#
# Libraries:
#   executive_base - Pure interface (INTERFACE, header-only)
#   executive_core - Shared concrete base (INTERFACE, header-only)
#   system_core_executive - POSIX implementation (SHARED)
#   executive_mcu - MCU implementation (INTERFACE, header-only)
# ==============================================================================

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

# Base interface library (IExecutive)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/base/CMakeLists.txt")
  add_subdirectory(base)
endif ()

# Core library (ExecutiveCore - shared concrete base)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/core/CMakeLists.txt")
  add_subdirectory(core)
endif ()

# POSIX implementation library (PosixExecutiveBase / ApexExecutive)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/posix/CMakeLists.txt")
  add_subdirectory(posix)
endif ()

# MCU implementation library (McuExecutive)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mcu/CMakeLists.txt")
  add_subdirectory(mcu)
endif ()

# Watchdog supervisor binary (POSIX only, no Apex deps)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/watchdog/CMakeLists.txt")
  add_subdirectory(watchdog)
endif ()
