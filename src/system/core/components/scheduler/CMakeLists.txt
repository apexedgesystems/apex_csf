# ==============================================================================
# scheduler - Task scheduling implementations
#
# Directory structure:
#   base/ - IScheduler interface (header-only, no heavy deps)
#   apex/ - SchedulerBase, SchedulerMultiThread, SchedulerSingleThread (full-featured)
#   lite/ - SchedulerLite (minimal for MCUs)
#
# Libraries:
#   scheduler_base - Pure interface (INTERFACE, header-only)
#   system_core_scheduler - Full implementation (SHARED)
# ==============================================================================

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

# Base interface library (IScheduler)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/base/CMakeLists.txt")
  add_subdirectory(base)
endif ()

# Apex implementation library (SchedulerBase, SchedulerMultiThread, SchedulerSingleThread)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apex/CMakeLists.txt")
  add_subdirectory(apex)
endif ()

# Lite implementation library (SchedulerLite)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lite/CMakeLists.txt")
  add_subdirectory(lite)
endif ()
