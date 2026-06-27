# ==============================================================================
# Performance Tests: sim_sensors
#
# Benchmarks the per-sample measurement path of each sensor and the underlying
# GaussianSampler draw.
# ==============================================================================

include(apex/All)

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Sensors_pTest.cpp")

apex_add_ptest(TARGET SimSensors_PTEST SOURCES ${PERF_TEST_SOURCES} LINK sim_sensors)
