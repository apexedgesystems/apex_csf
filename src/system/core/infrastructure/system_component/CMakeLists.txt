# ==============================================================================
# system_component - Component base classes and lifecycle management
#
# Directory structure:
#   base/  - IComponent interface (header-only, no heavy deps)
#   core/  - ComponentCore shared concrete base (header-only, no heavy deps)
#   posix/ - SystemComponentBase hierarchy (full-featured POSIX tier)
#   mcu/   - McuComponentBase (minimal MCU tier)
#
# Libraries:
#   system_component_base - Pure interface (INTERFACE, header-only)
#   system_component_core - Shared concrete base (INTERFACE, header-only)
#   system_core_system_component - POSIX implementation (SHARED)
#   system_component_mcu - MCU implementation (INTERFACE, header-only)
# ==============================================================================

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

# Base interface library (IComponent, ComponentType, Status)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/base/CMakeLists.txt")
  add_subdirectory(base)
endif ()

# Core library (ComponentCore - shared concrete base)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/core/CMakeLists.txt")
  add_subdirectory(core)
endif ()

# POSIX implementation library (SystemComponentBase hierarchy)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/posix/CMakeLists.txt")
  add_subdirectory(posix)
endif ()

# MCU implementation library (McuComponentBase)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mcu/CMakeLists.txt")
  add_subdirectory(mcu)
endif ()
