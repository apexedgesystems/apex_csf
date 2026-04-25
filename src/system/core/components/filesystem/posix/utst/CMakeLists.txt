# ==============================================================================
# Tests: system_core_filesystem
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Test Sources
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/FileSystemBase_uTest.cpp"
                      "${CMAKE_CURRENT_SOURCE_DIR}/ApexFileSystem_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreFilesystem
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_filesystem
  system_core_logs
  fmt::fmt
  LABELS
  system
  core
  filesystem
)
