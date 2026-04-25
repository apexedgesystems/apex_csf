# ==============================================================================
# action - Runtime action orchestration component
#
# Directory structure:
#   apex/ - ActionComponent (wraps ActionInterface)
#
# Libraries:
#   system_core_action - Full implementation (SHARED)
# ==============================================================================

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apex/CMakeLists.txt")
  add_subdirectory(apex)
endif ()
