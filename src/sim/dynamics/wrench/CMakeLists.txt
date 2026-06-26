# ==============================================================================
# Module: sim_dynamics_wrench
#
# Compositional force/moment aggregation -- the symmetric pattern to
# mass_properties. WrenchAccumulator stacks per-source applied forces
# (each at a point, plus an optional pure couple) into the net force and
# net moment about a chosen reference point (induced moment r x F via the
# parallel-force shift). Feeds the 6-DOF derivative as (force, moment).
# Wraps sim::dynamics::rigid_body::Vec3 + cross.
# ==============================================================================

include(apex/All)
apex_module(sim_dynamics_wrench)

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

target_link_libraries(
  ${LIB_NAME}
  INTERFACE sim_dynamics_rigid_body # Vec3 + cross
)

apex_standard_optins(${LIB_NAME})

apex_add_test_subdirs(utst ptst)
