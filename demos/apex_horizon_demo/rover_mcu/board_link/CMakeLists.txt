# ==============================================================================
# apex_horizon_demo_rover_board_link
#
# Host side of the serial link to the rover controller on the NUCLEO: drains
# the board's commands and heartbeats into the snapshot the RoverController
# forwards, judges the link, sends the rover's state.
# ==============================================================================

include(apex/All)

apex_module(apex_horizon_demo_rover_board_link)

apex_add_interface_library(
  NAME
  ${LIB_NAME}
  INC
  "${INC_DIR}"
  DEPS_INTERFACE
  apex_horizon_demo_ground_vehicle
  apex_horizon_demo_rover_controller
  apex_horizon_demo_rover_board
  system_core_protocols_serial_uart
  system_core_protocols_framing_slip
  system_core_system_component
  fmt::fmt
)

apex_standard_optins(${LIB_NAME})

apex_add_test_subdirs()
