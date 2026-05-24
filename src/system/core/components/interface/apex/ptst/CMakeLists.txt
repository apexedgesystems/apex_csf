# ==============================================================================
# Performance Tests: system_core_interface
#
# Buffer pool, queue, internal bus, APROTO codec, SLIP framing, and full
# RX/TX pipeline benchmarks for the Apex inter-component interface.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ApexInterface_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET
  ApexInterface_PTEST
  SOURCES
  ${PERF_TEST_SOURCES}
  LINK
  system_core_interface
  system_core_registry
  system_core_logs
  system_core_protocols_aproto
  utilities_concurrency
  fmt::fmt
)
