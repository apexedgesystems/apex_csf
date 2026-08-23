# ==============================================================================
# Module: apex_horizon_demo_aircraft (header-only)
#
# Aircraft SW model. Composes the sim/* primitives:
#   - sim_dynamics_rigid_body     (RigidBody6DOF inertial integration)
#   - sim_dynamics_integrators    (RK4, header-only)
#   - sim_aerodynamics            (PolarAero + StabilityDerivativeAero)
#   - sim_propulsion              (DensityScaledThrust — empirical engine)
#   - sim_environment_celestial_body (atmosphere + gravity dispatch)
#
# Two scheduled tasks: aircraftStep @ 50 Hz (the executive fundamental)
# + telemetry @ 1 Hz.
# ==============================================================================

include(apex/All)
apex_module(apex_horizon_demo_aircraft)

apex_add_interface_library(
  NAME
  ${LIB_NAME}
  INC
  "${INC_DIR}"
  DEPS_INTERFACE
  sim_aerodynamics
  sim_propulsion
  sim_dynamics_rigid_body
  sim_dynamics_mass_properties # FuelBurnMassProperties
  sim_dynamics_disturbance # Dryden turbulence
  sim_sensors # Pitot
  sim_dynamics_integrators
  sim_environment_celestial_body
  utilities_math_vecmat
  system_core_system_component
  utilities_helpers
  fmt::fmt
)

apex_standard_optins(${LIB_NAME})

apex_add_test_subdirs(utst)
