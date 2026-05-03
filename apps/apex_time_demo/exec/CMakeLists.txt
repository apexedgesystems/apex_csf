# ==============================================================================
# Time Demo executable
# ==============================================================================

include(apex/All)

set(APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

apex_add_app(
  NAME
  ApexTimeDemo
  SRC
  ${APP_SOURCES}
  LINK
  system_core_time_server
  system_core_action
  hal_mock
  hal_interface
  fmt::fmt
  REQUIRES
  POSIX
)
