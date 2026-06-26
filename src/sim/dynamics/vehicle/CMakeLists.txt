# ==============================================================================
# Module: sim_dynamics_vehicle
#
# The assembled vehicle: steps the 6-DOF rigid body from composed mass
# properties + net force/moment. Sits above the leaf modules (it includes
# rigid_body + mass_properties + wrench) so those stay free of any
# upward dependency -- one-directional includes, no cycles.
# ==============================================================================

include(apex/All)
apex_module(sim_dynamics_vehicle)

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

target_link_libraries(
  ${LIB_NAME}
  INTERFACE sim_dynamics_rigid_body sim_dynamics_mass_properties sim_dynamics_wrench
)

apex_standard_optins(${LIB_NAME})

apex_add_test_subdirs(utst ptst)
