# ==============================================================================
# Module: apex_horizon_demo_ground_vehicle (header-only)
#
# Kinematic rover over the attached CelestialBody's terrain: first-order
# speed, steered heading, surface clamp, 4-point slope sampling, and a
# forward lidar sweep. Publishes the 256-byte ROVR/1 telemetry the
# bridge streams to the visualization consumer.
# ==============================================================================

include(apex/All)
apex_module(apex_horizon_demo_ground_vehicle)

apex_add_interface_library(
  NAME
  ${LIB_NAME}
  INC
  "${INC_DIR}"
  DEPS_INTERFACE
  sim_environment_celestial_body
  system_core_system_component
  utilities_helpers
  utilities_math_vecmat
  fmt::fmt
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs()
