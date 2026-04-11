# ==============================================================================
# Ops Demo executable
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (2)
# ------------------------------------------------------------------------------

set(APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/OpsExecutive.cpp"
)

# ------------------------------------------------------------------------------
# Application
# ------------------------------------------------------------------------------

apex_add_app(
  NAME
  ApexOpsDemo
  SRC
  ${APP_SOURCES}
  INC
  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  LINK
  apex_ops_demo_wave
  system_core_executive
  system_core_scheduler
  system_core_support_system_monitor
  system_core_support_telemetry_manager
  system_core_logs
  system_core_filesystem
  utilities_helpers
  utilities_concurrency
  fmt::fmt
  REQUIRES
  POSIX
)
