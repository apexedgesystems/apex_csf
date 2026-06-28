# ==============================================================================
# Performance Tests: sim_gnc_aircraft
# ==============================================================================

include(apex/All)

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/AircraftControllers_pTest.cpp")

apex_add_ptest(TARGET SimGncAircraft_PTEST SOURCES ${PERF_TEST_SOURCES} LINK sim_gnc_aircraft)
