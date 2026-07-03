# ==============================================================================
# lidar_box demo executable
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (2)
# ------------------------------------------------------------------------------

set(APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/LidarBoxExecutive.cpp"
)

# ------------------------------------------------------------------------------
# Application
# ------------------------------------------------------------------------------

apex_add_app(
  NAME
  ApexLidarBoxDemo
  SRC
  ${APP_SOURCES}
  INC
  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  LINK
  apex_horizon_demo_lidar_box_producer
  system_core_registry
  system_core_support_shm_ring_bridge
  system_core_executive
  system_core_scheduler
  system_core_logs
  system_core_filesystem
  utilities_helpers
  utilities_concurrency
  fmt::fmt
  REQUIRES
  POSIX
)

# Deployment: single executive.
apex_add_deployment(
  NAME ApexLidarBoxDemo EXEC ApexLidarBoxDemo TPRM
  apps/apex_horizon_demo/lidar_box/tprm/master.tprm
)
