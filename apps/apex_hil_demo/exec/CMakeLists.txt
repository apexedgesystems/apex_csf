# ==============================================================================
# App: ApexHilDemo
#
# POSIX executive for the HIL flight demonstration. Runs the HilPlantModel
# under ApexExecutive with TPRM-driven configuration (task frequencies,
# thread affinity, plant parameters).
#
# Build:
#   make compose-debug
#   make compose-rpi-debug
#
# Run:
#   ./build/native-linux-debug/bin/ApexHilDemo \
#     --config apps/apex_hil_demo/tprm/master.tprm --shutdown-after 5
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (2 files)
# ------------------------------------------------------------------------------

set(APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/HilExecutive.cpp"
)

# ------------------------------------------------------------------------------
# Executable
# ------------------------------------------------------------------------------

apex_add_app(
  NAME
  ApexHilDemo
  SRC
  ${APP_SOURCES}
  INC
  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  "${CMAKE_CURRENT_SOURCE_DIR}/../common/inc"
  LINK
  apex_hil_demo_plant
  system_core_executive
  system_core_scheduler
  system_core_support_system_monitor
  system_core_logs
  system_core_filesystem
  system_core_protocols_serial_uart
  system_core_protocols_framing_slip
  utilities_checksums_crc
  sim_environment_gravity
  utilities_helpers
  utilities_concurrency
  fmt::fmt
  REQUIRES
  POSIX
)
