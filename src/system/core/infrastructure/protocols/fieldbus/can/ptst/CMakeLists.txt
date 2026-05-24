# ==============================================================================
# Performance Tests: system_core_protocols_fieldbus_can
#
# CAN bus loopback latency, send throughput, batch recv, and event loop overhead.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/CanBusAdapter_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET
  CanBusAdapter_PTEST
  SOURCES
  ${PERF_TEST_SOURCES}
  LINK
  system_core_protocols_fieldbus_can
  utilities_concurrency
)
