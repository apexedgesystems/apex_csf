# ==============================================================================
# Performance Tests: apex_horizon_demo_rover_controller
#
# Per-tick cost of the rover plant at 100 Hz (with and without the strobe and
# the sequence trace running) and the 10 Hz controller step.
# ==============================================================================

include(apex/All)

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/RoverStep_pTest.cpp")

apex_add_ptest(
  TARGET ApexHorizonDemoRoverController_PTEST SOURCES ${PERF_TEST_SOURCES} LINK
  apex_horizon_demo_rover_controller
)
