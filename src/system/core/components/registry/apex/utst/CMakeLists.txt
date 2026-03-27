# ==============================================================================
# Unit Tests: system_core_registry
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Test Sources (3)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/RegistryStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/RegistryData_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ApexRegistry_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreRegistry
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_registry
  system_core_scheduler
  fmt::fmt
  LABELS
  system
  core
  registry
)
