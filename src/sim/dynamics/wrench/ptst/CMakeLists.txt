# ==============================================================================
# Performance Tests: sim_dynamics_wrench
#
# Benchmarks the net force + moment combine about a reference point (the
# per-tick force re-stack).
# ==============================================================================

include(apex/All)

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Wrench_pTest.cpp")

apex_add_ptest(TARGET SimDynamicsWrench_PTEST SOURCES ${PERF_TEST_SOURCES} LINK sim_dynamics_wrench)
