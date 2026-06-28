# ==============================================================================
# Performance Tests: sim_gnc_common
# ==============================================================================

include(apex/All)

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/PIDLoop_pTest.cpp")

apex_add_ptest(TARGET SimGncCommon_PTEST SOURCES ${PERF_TEST_SOURCES} LINK sim_gnc_common)
