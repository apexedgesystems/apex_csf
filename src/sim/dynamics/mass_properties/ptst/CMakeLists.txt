# ==============================================================================
# Performance Tests: sim_dynamics_mass_properties
#
# Benchmarks the fuel-burn mass update and the whole-body parallel-axis
# accumulator (the per-tick mass re-stack).
# ==============================================================================

include(apex/All)

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/MassProperties_pTest.cpp")

apex_add_ptest(
  TARGET SimDynamicsMassProperties_PTEST SOURCES ${PERF_TEST_SOURCES} LINK
  sim_dynamics_mass_properties
)
