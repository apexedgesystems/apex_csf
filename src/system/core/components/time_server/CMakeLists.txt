# ==============================================================================
# time_server - PPS-driven UTC distribution component
#
# Directory structure:
#   apex/ - TimeServer (sole time authority for the system)
#
# Libraries:
#   system_core_time_server - Full implementation (SHARED)
# ==============================================================================

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apex/CMakeLists.txt")
  add_subdirectory(apex)
endif ()
