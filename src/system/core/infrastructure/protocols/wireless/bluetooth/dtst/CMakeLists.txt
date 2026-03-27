# ------------------------------------------------------------------------------
# Development Tests
# ------------------------------------------------------------------------------

set(DEV_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/RfcommEmulation_dTest.cpp")

apex_add_devtest(
  TARGET
  RfcommEmulation_Dev
  SOURCES
  ${DEV_TEST_SOURCES}
  LINK
  system_core_protocols_wireless_bluetooth
  fmt::fmt
)
