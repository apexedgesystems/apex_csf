# ==============================================================================
# executive - Executive implementations for system control
#
# Directory structure:
#   base/ - IExecutive interface (header-only, no heavy deps)
#   core/ - ExecutiveCore shared concrete base (header-only, no heavy deps)
#   apex/ - ApexExecutive (full-featured for resource-rich systems)
#   lite/ - LiteExecutive (minimal for MCUs)
#
# Libraries:
#   executive_base - Pure interface (INTERFACE, header-only)
#   executive_core - Shared concrete base (INTERFACE, header-only)
#   system_core_executive - Full implementation (SHARED)
#   executive_lite - MCU implementation (INTERFACE, header-only)
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

# Apex implementation library (ApexExecutive)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apex/CMakeLists.txt")
  add_subdirectory(apex)
endif ()

# Lite implementation library (LiteExecutive)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lite/CMakeLists.txt")
  add_subdirectory(lite)
endif ()

# Watchdog supervisor binary (POSIX only, no Apex deps)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/watchdog/CMakeLists.txt")
  add_subdirectory(watchdog)
endif ()
