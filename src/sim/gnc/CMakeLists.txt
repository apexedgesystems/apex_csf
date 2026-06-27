# ==============================================================================
# Module: sim_gnc
#
# Guidance, navigation, and control laws for atmospheric vehicles. Header-only: a
# generic PID primitive (PIDLoop) and the named loops it composes -- longitudinal
# (pitch attitude, altitude, speed hold), lateral-directional (roll, heading, yaw
# damper), and a vertical-gust feedforward (GustAlleviation). MCU-safe
# (allocation-free, fixed-size, no exceptions), parameterized, and
# vehicle-agnostic -- a specific aircraft's gains come from the application.
# ==============================================================================

include(apex/All)
apex_module(sim_gnc)

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

apex_standard_optins(${LIB_NAME})

apex_add_test_subdirs(utst ptst)
