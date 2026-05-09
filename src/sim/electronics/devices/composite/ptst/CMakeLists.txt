# ==============================================================================
# Performance Tests: sim_electronics_devices_composite
#
# CMOS gate truth table and stamp throughput.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Composite_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET Composite_PTEST SOURCES ${PERF_TEST_SOURCES} LINK sim_electronics_devices_composite
)
