# ==============================================================================
# Unit tests: the demos' hot-swap test plugin
# ==============================================================================

include(apex/All)

apex_add_gtest(
  TARGET
  TestDemoTestPlugin
  SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/TestPlugin_uTest.cpp"
  LINK
  system_core_system_component
  LABELS
  demos
  test_plugin
)
