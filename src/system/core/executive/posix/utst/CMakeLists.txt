# ==============================================================================
# Unit Tests: system_core_executive
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Test Sources (8)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/ExecutiveStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/RTMode_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ExecutiveData_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ShutdownConfig_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TickLateness_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ClockGrid_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ExecutiveCli_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ReloadLibrary_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreExecutive
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_executive
  # The executive embeds these by value in its header, so a test that
  # constructs one links them directly (the library keeps them private).
  system_core_scheduler
  system_core_filesystem
  system_core_system_component
  system_core_logs
  fmt::fmt
  LABELS
  system
  core
  executive
)

# The plugin library the RELOAD_LIBRARY test swaps in: the plugin build of
# the component the test links. Nothing links it.
set(_test_plugin_dir "${CMAKE_BINARY_DIR}/test_plugins/executive")
add_library(
  ExecutiveTestPlugin_Reload MODULE "${CMAKE_CURRENT_SOURCE_DIR}/plugins/ReloadTargetPlugin.cpp"
)
target_include_directories(ExecutiveTestPlugin_Reload PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(
  ExecutiveTestPlugin_Reload
  PRIVATE system_core_system_component system_core_logs
)
set_target_properties(
  ExecutiveTestPlugin_Reload
  PROPERTIES PREFIX ""
             SUFFIX ".so"
             LIBRARY_OUTPUT_DIRECTORY "${_test_plugin_dir}"
)
add_dependencies(TestSystemCoreExecutive ExecutiveTestPlugin_Reload)
target_compile_definitions(
  TestSystemCoreExecutive PRIVATE APEX_TEST_PLUGIN_DIR="${_test_plugin_dir}"
)
