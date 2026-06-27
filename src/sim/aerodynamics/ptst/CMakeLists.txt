# ==============================================================================
# Performance Tests: sim_aerodynamics
#
# Benchmarks the per-tick aero evaluators: the parabolic polar and the
# stability-derivative force/moment build-up.
# ==============================================================================

include(apex/All)

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Aerodynamics_pTest.cpp")

apex_add_ptest(TARGET SimAerodynamics_PTEST SOURCES ${PERF_TEST_SOURCES} LINK sim_aerodynamics)
