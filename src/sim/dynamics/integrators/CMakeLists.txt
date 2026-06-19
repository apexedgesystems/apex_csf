# ==============================================================================
# Module: sim_dynamics_integrators
#
# Generic ODE integrators: ForwardEuler (baseline) and RK4 (default).
# Header-only template functions; the integrator state is owned by the
# caller (rigid-body model).
# ==============================================================================

include(apex/All)
apex_module(sim_dynamics_integrators)

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

apex_standard_optins(${LIB_NAME})

apex_add_test_subdirs(utst)
