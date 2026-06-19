# ==============================================================================
# Module: sim_dynamics_disturbance
#
# Atmospheric disturbance models (turbulence, wind shear, gusts) that
# couple into the aircraft's apparent wind. Provides DrydenTurbulence
# (3-axis Dryden PSD-shaped white-noise filters per MIL-HDBK-1797, the
# public specification of the Dryden gust spectra).
# ==============================================================================

include(apex/All)
apex_module(sim_dynamics_disturbance)

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

apex_standard_optins(${LIB_NAME})

apex_add_test_subdirs(utst)
