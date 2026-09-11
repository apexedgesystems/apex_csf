# ==============================================================================
# Performance Tests: apex_horizon_demo_aircraft_controller
#
# Per-tick cost of the aircraft plant (trace idle / trace armed) and the
# 25 Hz closed-loop tick as the demo scheduler runs them.
# ==============================================================================

include(apex/All)

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/AircraftStep_pTest.cpp")

apex_add_ptest(
  TARGET ApexHorizonDemoAircraftController_PTEST SOURCES ${PERF_TEST_SOURCES} LINK
  apex_horizon_demo_aircraft_controller
)
