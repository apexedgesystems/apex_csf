# ==============================================================================
# Unit tests: HIL demo hot-swap test plugin
# ==============================================================================

include(apex/All)

apex_add_gtest(
  TARGET
  TestHilDemoTestPlugin
  SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/TestPlugin_uTest.cpp"
  LINK
  system_core_system_component
  LABELS
  apex_hil_demo
  test_plugin
)
