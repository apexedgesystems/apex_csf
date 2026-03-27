# ==============================================================================
# Test plugins for hot-swap testing
#
# Builds two shared libraries with different version numbers:
#   - TestPlugin_v1.so (VERSION=1)
#   - TestPlugin_v2.so (VERSION=2)
#
# These are NOT installed or linked into the main app. They exist
# purely for runtime dlopen hot-swap testing via RELOAD_LIBRARY.
# ==============================================================================

# Version 1 plugin
add_library(TestPlugin_v1 SHARED TestPlugin_v1.cpp)
target_include_directories(TestPlugin_v1 PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(
  TestPlugin_v1
  PRIVATE system_core_system_component
)
set_target_properties(
  TestPlugin_v1
  PROPERTIES PREFIX ""
             SUFFIX ".so"
             LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test_plugins"
)

# Version 2 plugin
add_library(TestPlugin_v2 SHARED TestPlugin_v2.cpp)
target_include_directories(TestPlugin_v2 PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(
  TestPlugin_v2
  PRIVATE system_core_system_component
)
set_target_properties(
  TestPlugin_v2
  PROPERTIES PREFIX ""
             SUFFIX ".so"
             LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test_plugins"
)
