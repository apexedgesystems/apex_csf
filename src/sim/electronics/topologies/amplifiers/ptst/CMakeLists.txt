# ==============================================================================
# Performance Tests: sim_electronics_topologies_amplifiers
#
# BjtCommonEmitter circuit-build + DC operating-point solve throughput.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Amplifiers_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET
  Amplifiers_PTEST
  SOURCES
  ${PERF_TEST_SOURCES}
  LINK
  sim_electronics_topologies_amplifiers
  sim_electronics_circuit
  sim_electronics_devices_nonlinear
)
