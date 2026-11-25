# ==============================================================================
# Unit tests: ops demo hot-swap test plugin
# ==============================================================================

include(apex/All)

apex_add_gtest(
  TARGET
  TestOpsDemoTestPlugin
  SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/TestPlugin_uTest.cpp"
  LINK
  system_core_system_component
  LABELS
  apex_ops_demo
  test_plugin
)
