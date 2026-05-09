# ==============================================================================
# Performance Tests: sim_electronics_topologies_filters
#
# Throughput benchmarks for RC low-pass filter construction, simulation,
# and analytical reference functions.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Filters_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET
  Filters_PTEST
  SOURCES
  ${PERF_TEST_SOURCES}
  LINK
  sim_electronics_topologies_filters
  sim_electronics_circuit
  sim_electronics_devices_linear
  sim_electronics_algorithms_transient
  LABELS
  sim
  electronics
  filters
  performance
)
