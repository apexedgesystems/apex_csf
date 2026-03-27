# ==============================================================================
# Module: sim_analog_regulator
#
# Header-only LDO voltage regulator model for Monte Carlo tolerance analysis.
#
# Components:
#   RegulatorModel  - Resistor divider + output cap + ESR transient model
#
# Design:
#   Closed-form computation (no ODE integration). Runs in microseconds.
#   Designed for parameter sweep via MonteCarloDriver.
#
# Libraries:
#   sim_analog_regulator - Header-only interface library
# ==============================================================================

include(apex/All)
apex_module(sim_analog_regulator)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst)
