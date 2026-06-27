# ==============================================================================
# Performance Tests: sim_gnc
#
# Benchmarks the per-step cost of the PID primitive and the named control loops.
# ==============================================================================

include(apex/All)

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Gnc_pTest.cpp")

apex_add_ptest(TARGET SimGnc_PTEST SOURCES ${PERF_TEST_SOURCES} LINK sim_gnc)
