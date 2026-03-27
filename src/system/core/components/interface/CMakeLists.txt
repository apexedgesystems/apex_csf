# ==============================================================================
# interface - External command/telemetry interface implementations
#
# Directory structure:
#   apex/ - InterfaceBase, ApexInterface (full-featured queue-based async)
#   lite/ - LiteInterface (minimal static mailboxes) [future]
#
# Libraries:
#   system_core_interface - Full implementation (SHARED)
# ==============================================================================

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

# Apex implementation library (InterfaceBase, ApexInterface)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apex/CMakeLists.txt")
  add_subdirectory(apex)
endif ()

# Lite implementation library (LiteInterface) [future]
# if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lite/CMakeLists.txt")
#   add_subdirectory(lite)
# endif ()
