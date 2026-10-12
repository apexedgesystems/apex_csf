# ==============================================================================
# ApexRoverMcuDemo -- the rover with its controller behind a seam, lamps, and
# the sequence catalog (software-in-the-loop form; the controller's seat is
# taken by a UART driver in the hardware form).
# ==============================================================================

include(apex/All)

set(APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/RoverMcuExecutive.cpp"
)

apex_add_app(
  NAME
  ApexRoverMcuDemo
  SRC
  ${APP_SOURCES}
  INC
  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  LINK
  apex_horizon_demo_ground_vehicle
  apex_horizon_demo_rover_controller
  apex_horizon_demo_rover_board_link
  apex_horizon_demo_rover_lidar_model
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

apex_add_tprm(NAME ApexRoverMcuDemo MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/../tprm/tprm.manifest")

apex_add_deployment(NAME ApexRoverMcuDemo EXEC ApexRoverMcuDemo TPRM ApexRoverMcuDemo/master.tprm)

# The app directory sets no apex_module() paths, so the test tier is added
# directly rather than through apex_add_test_subdirs().
add_subdirectory(utst)
