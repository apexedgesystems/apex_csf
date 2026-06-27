# ==============================================================================
# Performance Tests: sim_propulsion
#
# Benchmarks the per-tick engine models: the density-scaled thrust evaluation
# and the two-spool turbofan step.
# ==============================================================================

include(apex/All)

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Propulsion_pTest.cpp")

apex_add_ptest(TARGET SimPropulsion_PTEST SOURCES ${PERF_TEST_SOURCES} LINK sim_propulsion)
