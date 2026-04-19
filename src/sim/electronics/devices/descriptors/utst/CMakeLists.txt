# ==============================================================================
# Unit Tests: sim_electronics_devices_descriptors
# ==============================================================================

set(TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/CapacitorDescriptor_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/InductorDescriptor_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/NonlinearDescriptors_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ResistorDescriptor_uTest.cpp"
)

apex_add_gtest(
  TARGET
  sim_electronics_devices_descriptors_uTest
  SOURCES
  ${TEST_SOURCES}
  LINK
  sim_electronics_devices_descriptors
  COVERAGE_FOR
  sim_electronics_devices_descriptors
  LABELS
  devices
)
