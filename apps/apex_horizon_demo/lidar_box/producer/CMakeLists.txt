# ==============================================================================
# Module: apex_horizon_demo_lidar_box_producer
#
# The LidarBoxProducer SW model: advances the body's Lissajous drift + yaw,
# measures the six wall clearances with the sim_sensors BoxClearanceLidar, and
# publishes the LidarBoxFrame OUTPUT block the bridge streams. Header-only.
# ==============================================================================

include(apex/All)
apex_module(apex_horizon_demo_lidar_box_producer)

apex_add_interface_library(
  NAME ${LIB_NAME}
  INC "${INC_DIR}"
  DEPS_INTERFACE sim_sensors system_core_system_component
)

apex_standard_optins(${LIB_NAME})

apex_add_test_subdirs()
