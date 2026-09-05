# ==============================================================================
# aircraft_atmo demo executable
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (2)
# ------------------------------------------------------------------------------

set(APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/AircraftAtmoExecutive.cpp"
)

# ------------------------------------------------------------------------------
# Application
# ------------------------------------------------------------------------------

apex_add_app(
  NAME
  ApexAircraftAtmoDemo
  SRC
  ${APP_SOURCES}
  INC
  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  LINK
  apex_horizon_demo_aircraft
  apex_horizon_demo_aircraft_controller
  sim_environment_celestial_body
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

# Deployment: single executive; the master is generated from the manifest.
apex_add_tprm(
  NAME ApexAircraftAtmoDemo MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/../tprm/tprm.manifest"
)
apex_add_deployment(
  NAME
  ApexAircraftAtmoDemo
  EXEC
  ApexAircraftAtmoDemo
  TPRM
  ApexAircraftAtmoDemo/master.tprm
  DATA
  demos/apex_horizon_demo/aircraft_atmo/data/earth_ussa76.atm
)
