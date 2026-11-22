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
# Test Plugins
# ------------------------------------------------------------------------------

# The plugin libraries the RELOAD_LIBRARY tests swap in: the plugin build of
# the component the test links, one with its id under another name, and two
# that the swap must refuse (a shorter output block, an unsequenced task).
foreach (_plugin Reload RenamedReload ShorterOutputReload UnsequencedReload)
  apex_add_test_plugin(
    TARGET
    ExecutiveTestPlugin_${_plugin}
    SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/${_plugin}TargetPlugin.cpp"
    LINK
    system_core_system_component
    system_core_logs
  )
endforeach ()

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
  # The reload cases send framed commands over the interface's TCP port.
  system_core_protocols_aproto
  system_core_protocols_framing_slip
  system_core_protocols_network_tcp
  fmt::fmt
  PLUGINS
  ExecutiveTestPlugin_Reload
  ExecutiveTestPlugin_RenamedReload
  ExecutiveTestPlugin_ShorterOutputReload
  ExecutiveTestPlugin_UnsequencedReload
  LABELS
  system
  core
  executive
)
