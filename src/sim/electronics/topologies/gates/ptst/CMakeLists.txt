# ==============================================================================
# Performance Tests: sim_electronics_topologies_gates
#
# Gate evaluation and adder throughput benchmarks.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Gates_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(TARGET Gates_PTEST SOURCES ${PERF_TEST_SOURCES} LINK sim_electronics_topologies_gates)
