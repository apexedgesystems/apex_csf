# ==============================================================================
# Performance Tests: sim_dynamics_vehicle
#
# Benchmarks the steady-state composed tick: re-stack mass + forces, then one
# 6-DOF RK4 step from the aggregates.
# ==============================================================================

include(apex/All)

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Vehicle_pTest.cpp")

apex_add_ptest(
  TARGET SimDynamicsVehicle_PTEST SOURCES ${PERF_TEST_SOURCES} LINK sim_dynamics_vehicle
)
