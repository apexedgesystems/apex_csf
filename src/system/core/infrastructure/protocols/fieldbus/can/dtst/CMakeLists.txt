# ==============================================================================
# Development Tests: system_core_protocols_fieldbus_can
#
# Requires vcan kernel module and physical CAN hardware.
#
#   sudo modprobe vcan
#   ./build/native-linux-debug/bin/dtests/CanBus_Dev
# ==============================================================================

# ------------------------------------------------------------------------------
# Development Test Sources (1 file)
# ------------------------------------------------------------------------------

set(DEV_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/00_CanBusEmulation_dTest.cpp")

# ------------------------------------------------------------------------------
# Development Tests
# ------------------------------------------------------------------------------

apex_add_devtest(
  TARGET
  CanBus_Dev
  SOURCES
  ${DEV_TEST_SOURCES}
  LINK
  system_core_protocols_fieldbus_can
  fmt::fmt
)
