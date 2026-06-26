# ==============================================================================
# Time Demo executable
# ==============================================================================

include(apex/All)

set(APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/TimeDemoExecutive.cpp"
)

apex_add_app(
  NAME
  ApexTimeDemo
  SRC
  ${APP_SOURCES}
  INC
  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  LINK
  system_core_executive
  system_core_scheduler
  system_core_support_system_monitor
  system_core_logs
  system_core_filesystem
  system_core_time_server
  hal_mock
  hal_interface
  utilities_helpers
  utilities_concurrency
  fmt::fmt
  REQUIRES
  POSIX
)

# Deployment: single executive (no TPRM).
apex_add_deployment(NAME ApexTimeDemo EXECS ApexTimeDemo)
