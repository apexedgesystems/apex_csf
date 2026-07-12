# ==============================================================================
# TemplateApp executable
# ==============================================================================

include(apex/All)

apex_add_app(
  NAME
  TemplateApp
  SRC
  "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
  LINK
  system_core_executive
  system_core_scheduler
  system_core_support_system_monitor
  system_core_logs
  system_core_filesystem
  hal_mock
  hal_interface
  utilities_helpers
  utilities_concurrency
  fmt::fmt
  REQUIRES
  POSIX
)

# Deployment: single executive, defaults only (no TPRM).
apex_add_deployment(NAME TemplateApp EXEC TemplateApp)
