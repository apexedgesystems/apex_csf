# ==============================================================================
# Performance Tests: sim_dynamics_rigid_body
#
# Benchmarks the rigid-body step primitives: PointMass3D (translational) and
# the full 13-state RigidBody6DOF step.
# ==============================================================================

include(apex/All)

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/RigidBody_pTest.cpp")

apex_add_ptest(
  TARGET SimDynamicsRigidBody_PTEST SOURCES ${PERF_TEST_SOURCES} LINK sim_dynamics_rigid_body
)
