# ==============================================================================
# Performance Tests: system_core_filesystem
#
# Path validation, directory accessor, atomic write, and status conversion
# microbenchmarks for the POSIX ApexFileSystem.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/FileSystem_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET
  FileSystem_PTEST
  SOURCES
  ${PERF_TEST_SOURCES}
  LINK
  system_core_filesystem
  fmt::fmt
)
