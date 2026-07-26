# ==============================================================================
# rover_terrain demo executable
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (2)
# ------------------------------------------------------------------------------

set(APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/RoverTerrainExecutive.cpp"
)

# ------------------------------------------------------------------------------
# Application
# ------------------------------------------------------------------------------

apex_add_app(
  NAME
  ApexRoverTerrainDemo
  SRC
  ${APP_SOURCES}
  INC
  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  LINK
  apex_horizon_demo_ground_vehicle
  apex_horizon_demo_world_query_probe
  sim_environment_celestial_body
  utilities_math_celestial
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
  NAME ApexRoverTerrainDemo EXEC ApexRoverTerrainDemo TPRM
  demos/apex_horizon_demo/rover_terrain/tprm/master.tprm
)
