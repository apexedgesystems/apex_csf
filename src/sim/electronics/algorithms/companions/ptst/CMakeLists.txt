# ==============================================================================
# Performance Tests: sim_electronics_algorithms_companions
#
# Reactive element companion evaluation and stamp throughput.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Companions_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET Companions_PTEST SOURCES ${PERF_TEST_SOURCES} LINK sim_electronics_algorithms_companions
)
