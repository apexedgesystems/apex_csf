# ==============================================================================
# Performance Tests: sim_electronics_circuit
#
# Circuit build + DC solve throughput and transient simulation throughput.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Circuit_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET
  Circuit_PTEST
  SOURCES
  ${PERF_TEST_SOURCES}
  LINK
  sim_electronics_circuit
  sim_electronics_devices_linear
)
