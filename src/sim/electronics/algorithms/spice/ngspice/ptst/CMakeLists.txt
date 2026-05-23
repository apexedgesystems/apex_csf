# ==============================================================================
# Performance Tests: sim_electronics_algorithms_spice_ngspice
#
# NgspiceWrapper init/shutdown and DC operating point overhead.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Ngspice_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET Ngspice_PTEST SOURCES ${PERF_TEST_SOURCES} LINK sim_electronics_algorithms_spice_ngspice
)
