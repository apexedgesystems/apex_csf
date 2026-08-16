# ==============================================================================
# Module: apex_horizon_demo_aircraft_controller (header-only)
#
# Autopilot SW model. Wraps the six named classical loops over
# the existing sim_gnc_aircraft PIDLoop primitive and reads from Aircraft
# (apex_horizon_demo_aircraft) via a direct C++ pointer set by the
# executive.
#
# Two scheduled tasks: controllerStep @ 25 Hz + telemetry @ 1 Hz.
# Aircraft runs at 50 Hz so the inner pitch/roll loops always see two
# state ticks of integration between control updates.
# ==============================================================================

include(apex/All)
apex_module(apex_horizon_demo_aircraft_controller)

apex_add_interface_library(
  NAME
  ${LIB_NAME}
  INC
  "${INC_DIR}"
  DEPS_INTERFACE
  apex_horizon_demo_aircraft # reads Aircraft::telemetry()
  sim_gnc_aircraft # PIDLoop + the six named classical loops
  system_core_system_component
  utilities_helpers
  fmt::fmt
)

apex_standard_optins(${LIB_NAME})

apex_add_test_subdirs(utst)
