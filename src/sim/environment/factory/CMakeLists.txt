# ==============================================================================
# Module: sim_environment_factory
#
# Cross-subsystem factory: builds gravity + terrain + atmosphere models
# polymorphically from (Body, fidelity). Sibling of sim_environment_gravity,
# sim_environment_terrain, and sim_environment_atmosphere; depends on all three.
#
# Library:
#   sim_environment_factory  - CPU implementation
#
# Mirrors sim_environment_gravity / sim_environment_terrain /
# sim_environment_atmosphere organization. See README.md for the full design.
# ==============================================================================

include(apex/All)
apex_module(sim_environment_factory)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/EnvironmentFactory.cpp")

# ------------------------------------------------------------------------------
# Library
# ------------------------------------------------------------------------------

apex_add_library(
  NAME
  ${LIB_NAME}
  TYPE
  SHARED
  INC
  "${INC_DIR}"
  SRC
  ${CPP_SOURCES}
  DEPS_PUBLIC
  sim_environment_gravity
  sim_environment_terrain
  sim_environment_atmosphere
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst)
