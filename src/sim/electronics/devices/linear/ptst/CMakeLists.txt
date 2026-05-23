# ==============================================================================
# Performance Tests: sim_electronics_devices_linear
#
# Resistor stamp and conductance calculation throughput.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Linear_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(TARGET Linear_PTEST SOURCES ${PERF_TEST_SOURCES} LINK sim_electronics_devices_linear)
