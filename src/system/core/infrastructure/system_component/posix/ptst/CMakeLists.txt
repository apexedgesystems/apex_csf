# ==============================================================================
# Performance Tests: system_core_system_component
#
# activeParams() RT-critical latency, load/apply/rollback lifecycle, scaling.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ParamBank_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET ParamBank_PTEST SOURCES ${PERF_TEST_SOURCES} LINK system_core_system_component
)
