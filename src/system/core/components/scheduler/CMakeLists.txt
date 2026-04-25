# ==============================================================================
# scheduler - Task scheduling implementations
#
# Directory structure:
#   base/  - IScheduler interface (header-only, no heavy deps)
#   posix/ - SchedulerBase, SchedulerMultiThread, SchedulerSingleThread (POSIX tier)
#   mcu/   - SchedulerLite (MCU tier)
#
# Libraries:
#   scheduler_base - Pure interface (INTERFACE, header-only)
#   system_core_scheduler - POSIX implementation (SHARED)
#   scheduler_mcu - MCU implementation (INTERFACE, header-only)
# ==============================================================================

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

# Base interface library (IScheduler)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/base/CMakeLists.txt")
  add_subdirectory(base)
endif ()

# POSIX implementation library (SchedulerBase, SchedulerMultiThread, SchedulerSingleThread)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/posix/CMakeLists.txt")
  add_subdirectory(posix)
endif ()

# MCU implementation library (SchedulerLite)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mcu/CMakeLists.txt")
  add_subdirectory(mcu)
endif ()
