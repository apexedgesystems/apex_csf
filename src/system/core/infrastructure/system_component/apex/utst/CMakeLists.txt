# ==============================================================================
# Unit Tests: system_core_system_component
#
# Tests Status enum, SystemComponentBase lifecycle (init/reset/idempotent),
# SystemComponent<T> A/B parameter staging (load/apply/rollback), and
# PackedTprm archive extraction and entry lookup.
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources (4 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/SystemComponentStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SystemComponentBase_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SystemComponent_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PackedTprm_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreSystemComponent
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_system_component
  system_core_logs
  utilities_helpers
  fmt::fmt
  LABELS
  system
  system_component
)
