# ==============================================================================
# apex_horizon_demo_rover_lidar_model
#
# The rover's lidar as a hardware model: each plant sweep goes out as one
# LIDAR_SCAN frame on a serial port wired to the board's sensor UART.
# ==============================================================================

include(apex/All)

apex_module(apex_horizon_demo_rover_lidar_model)

apex_add_interface_library(
  NAME
  ${LIB_NAME}
  INC
  "${INC_DIR}"
  DEPS_INTERFACE
  apex_horizon_demo_ground_vehicle
  apex_horizon_demo_rover_board
  system_core_protocols_serial_uart
  system_core_protocols_framing_slip
  system_core_system_component
  fmt::fmt
)

apex_standard_optins(${LIB_NAME})

# Struct dictionary for zenith (make apex-data-db)
apex_add_data_manifest(MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/lidar_model_data.toml")

apex_add_test_subdirs()
