# ==============================================================================
# Module: sim_gnc_aircraft
#
# Aircraft-class control laws: the named autopilot loops that compose the generic
# PID primitive -- longitudinal (pitch attitude, altitude, speed hold),
# lateral-directional (roll, heading, yaw damper), and a vertical-gust
# feedforward. They assume aerodynamic control surfaces and atmospheric flight, so
# they are grouped apart from other vehicle classes (spacecraft, rover, ...).
# Parameterized and vehicle-agnostic within the class -- a specific aircraft's
# gains come from the application.
# ==============================================================================

include(apex/All)
apex_module(sim_gnc_aircraft)

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

target_link_libraries(
  ${LIB_NAME}
  INTERFACE sim_gnc_common
) # PIDLoop primitive

apex_standard_optins(${LIB_NAME})

apex_add_test_subdirs(utst ptst)
