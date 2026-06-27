# ==============================================================================
# Module: sim_propulsion
#
# Engine thrust models for atmospheric vehicles. Header-only: a density-scaled
# empirical baseline (DensityScaledThrust) and a two-spool turbofan with N1/N2
# spool dynamics and rotor angular momentum (Turbofan2Spool). Parameterized and
# vehicle-agnostic -- a specific engine's parameters come from the application.
# ==============================================================================

include(apex/All)
apex_module(sim_propulsion)

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

apex_standard_optins(${LIB_NAME})

apex_add_test_subdirs(utst ptst)
