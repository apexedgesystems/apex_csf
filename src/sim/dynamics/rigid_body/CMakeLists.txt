# ==============================================================================
# Module: sim_dynamics_rigid_body
#
# Rigid-body equations of motion: PointMass3D (6-state translational
# kinematics) + RigidBody6DOF (12-state quaternion attitude).
# ==============================================================================

include(apex/All)
apex_module(sim_dynamics_rigid_body)

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

target_link_libraries(
  ${LIB_NAME}
  INTERFACE sim_dynamics_integrators
            utilities_math_integration # apex::math::integration::Quaternion
            utilities_math_quaternion # shared attitude kinematics rate
            utilities_math_vecmat # Vec3 helper + inertia delegation
)

apex_standard_optins(${LIB_NAME})

apex_add_test_subdirs(utst ptst)
