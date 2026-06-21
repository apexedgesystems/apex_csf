# ==============================================================================
# Module: sim_dynamics_mass_properties
#
# Compositional mass / CG / inertia for multi-part vehicles.
# Provides MassProperties::aggregate -- combines per-part MassContributors
# (mass, CG, own-CG inertia) into whole-body mass, mass-weighted CG, and
# the inertia about the net CG via the parallel-axis theorem. FuelBurn
# exposes a TSFC-driven fuel tank as one such (time-varying) contributor.
# Wraps sim::dynamics::rigid_body::InertiaTensor (full symmetric tensor).
# ==============================================================================

include(apex/All)
apex_module(sim_dynamics_mass_properties)

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

target_link_libraries(
  ${LIB_NAME}
  INTERFACE sim_dynamics_rigid_body # InertiaTensor + Vec3
)

apex_standard_optins(${LIB_NAME})

apex_add_test_subdirs(utst ptst)
