# ==============================================================================
# Module: sim_dynamics_mass_properties
#
# Time-varying mass / CG / inertia tensor for vehicles that burn fuel.
# Provides FuelBurnMassProperties -- TSFC-driven fuel burn rate,
# linear CG migration between full-fuel and empty-fuel reference points,
# linear inertia scaling. Wraps sim::dynamics::rigid_body::InertiaTensor
# for the tensor type.
# ==============================================================================

include(apex/All)
apex_module(sim_dynamics_mass_properties)

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

target_link_libraries(
  ${LIB_NAME}
  INTERFACE sim_dynamics_rigid_body # InertiaTensor + Vec3
)

apex_standard_optins(${LIB_NAME})

apex_add_test_subdirs(utst)
