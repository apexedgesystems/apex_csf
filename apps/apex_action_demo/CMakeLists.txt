# ==============================================================================
# apps/apex_action_demo/CMakeLists.txt - Action Engine Demo Application
#
# Demonstrates the action engine's observe-and-react pipeline with the
# DataTransform support component for runtime data mutation. Exercises
# watchpoints, event notifications, command sequences, and fault injection.
# ==============================================================================

if (NOT APEX_PLATFORM_BAREMETAL)
  add_subdirectory(sensor)
  add_subdirectory(exec)
endif ()
