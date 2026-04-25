# ==============================================================================
# system_component - Component base classes and lifecycle management
#
# Directory structure:
#   base/ - IComponent interface (header-only, no heavy deps)
#   core/ - ComponentCore shared concrete base (header-only, no heavy deps)
#   apex/ - SystemComponentBase hierarchy (full-featured for ApexExecutive)
#   lite/ - LiteComponentBase (minimal for LiteExecutive)
#
# Libraries:
#   system_component_base - Pure interface (INTERFACE, header-only)
#   system_component_core - Shared concrete base (INTERFACE, header-only)
#   system_core_system_component - Full implementation (SHARED)
#   system_component_lite - MCU implementation (INTERFACE, header-only)
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

# Apex implementation library (SystemComponentBase hierarchy)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apex/CMakeLists.txt")
  add_subdirectory(apex)
endif ()

# Lite implementation library (LiteComponentBase)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lite/CMakeLists.txt")
  add_subdirectory(lite)
endif ()
