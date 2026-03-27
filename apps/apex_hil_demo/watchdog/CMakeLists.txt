# ==============================================================================
# App: ApexWatchdog
#
# Standalone supervisor binary for the HIL demo. Wraps the framework-provided
# executive_watchdog library with CLI argument parsing and signal handling.
#
# Build:
#   make compose-debug
#
# Run:
#   ApexWatchdog [options] -- ./ApexHilDemo --config tprm/master.tprm
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Executable
# ------------------------------------------------------------------------------

apex_add_app(
  NAME
  ApexWatchdog
  SRC
  "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
  LINK
  executive_watchdog
  REQUIRES
  POSIX
)
