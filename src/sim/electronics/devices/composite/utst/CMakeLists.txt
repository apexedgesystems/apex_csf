# ==============================================================================
# Unit Tests: sim_electronics_devices_composite
# ==============================================================================

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/CompositeDevices_uTest.cpp")

apex_add_gtest(
  TARGET
  sim_electronics_devices_composite_uTest
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_electronics_devices_composite
  COVERAGE_FOR
  sim_electronics_devices_composite
  LABELS
  devices
)
