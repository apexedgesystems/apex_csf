# ==============================================================================
# Performance Tests: sim_electronics_chips_intel4004_behavioral
#
# Behavioral CPU instruction throughput: single step and multi-instruction
# program execution.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Intel4004Behavioral_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET Intel4004Behavioral_PTEST SOURCES ${PERF_TEST_SOURCES} LINK
  sim_electronics_chips_intel4004_behavioral
)
