# ==============================================================================
# Unit Tests: sim_electronics_devices_linear
# ==============================================================================

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/ResistorModel_uTest.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/CapacitorModel_uTest.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/InductorModel_uTest.cpp"
)

apex_add_gtest(
  TARGET
  sim_electronics_devices_linear_uTest
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_electronics_devices_linear
  COVERAGE_FOR
  sim_electronics_devices_linear
  LABELS
  sim
  electronics
  devices
  linear
)
