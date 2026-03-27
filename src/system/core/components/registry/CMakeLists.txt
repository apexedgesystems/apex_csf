# ==============================================================================
# registry - Component registry implementations
#
# Directory structure:
#   apex/ - ApexRegistry (full-featured runtime registration, RDAT export)
#   lite/ - StaticRegistry or NullRegistry (minimal for MCUs) [future]
#
# Libraries:
#   system_core_registry - Full implementation (SHARED)
# ==============================================================================

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

# Apex implementation library (ApexRegistry)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apex/CMakeLists.txt")
  add_subdirectory(apex)
endif ()

# Lite implementation library (StaticRegistry) [future]
# if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lite/CMakeLists.txt")
#   add_subdirectory(lite)
# endif ()
