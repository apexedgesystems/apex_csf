# ==============================================================================
# apex_horizon_demo_rover_controller
#
# Drive controller for the demo rover (HOLD / TRAJECTORY / WAYPOINT): reads the
# GroundVehicle's pose, writes the drive command the plant consumes.
# ==============================================================================

include(apex/All)

apex_module(apex_horizon_demo_rover_controller)

apex_add_interface_library(
  NAME
  ${LIB_NAME}
  INC
  "${INC_DIR}"
  DEPS_INTERFACE
  apex_horizon_demo_ground_vehicle # reads GroundVehicle::telemetry(), aliases its drive command
  system_core_system_component
  utilities_math_vecmat
  fmt::fmt
)

apex_standard_optins(${LIB_NAME})

apex_add_test_subdirs()
