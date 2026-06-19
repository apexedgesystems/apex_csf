# ==============================================================================
# Module: sim_environment_atmosphere
#
# Atmosphere models for aerospace simulation. Fidelity ladder:
#   ConstantAtmosphere    - vacuum sentinel + held-fixed sea-level conditions
#   ExponentialAtmosphere - isothermal exp(-h/H) analytic baseline
#   LayeredAtmosphere     - hydrostatic N-layer (USSA76-class)
#   earth/Ussa76AtmosphereModel - LayeredAtmosphere preloaded with USSA76
#   moon/VacuumAtmosphereModel  - ConstantAtmosphere locked to vacuum
#
# Library:
#   sim_environment_atmosphere  - CPU implementation
#
# Mirrors sim_environment_gravity / sim_environment_terrain organization.
# See README.md for the full design.
# ==============================================================================

include(apex/All)
apex_module(sim_environment_atmosphere)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/AtmosphereModule.cpp" "${SRC_DIR}/Atm.cpp"
                "${SRC_DIR}/LayeredAtmosphere.cpp"
)

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
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs()
