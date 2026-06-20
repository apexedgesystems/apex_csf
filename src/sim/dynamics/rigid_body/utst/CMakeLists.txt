# ==============================================================================
# Unit Tests: sim_dynamics_rigid_body
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/PointMass3D_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/RigidBody6DOF_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/VehicleStep_uTest.cpp"
)

apex_add_gtest(
  TARGET
  TestSimDynamicsRigidBody
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_dynamics_rigid_body
  sim_dynamics # VehicleStep.hpp aggregate-consuming overload
  LABELS
  sim
  dynamics
)
