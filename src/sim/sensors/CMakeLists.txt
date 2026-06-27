# ==============================================================================
# Module: sim_sensors
#
# Sensor measurement models for atmospheric vehicles. Header-only: a GNSS
# receiver (GPS), a Pitot indicated-airspeed model, and a radar altimeter, each
# mapping ground truth to a measurement with physical noise + bias. Noise comes
# from a deterministic, portable GaussianSampler so replay and Monte Carlo
# reproduce across toolchains and targets. Parameterized and vehicle-agnostic --
# a specific sensor's error figures come from the application.
# ==============================================================================

include(apex/All)
apex_module(sim_sensors)

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

apex_standard_optins(${LIB_NAME})

apex_add_test_subdirs(utst ptst)
