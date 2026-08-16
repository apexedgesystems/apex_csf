# ==============================================================================
# Module: apex_horizon_demo_world_query_probe (header-only)
#
# Schedulable apex component that exercises a CelestialBody by querying
# its gravity / terrain / atmosphere at configured survey points each
# tick and publishing the sampled state as OUTPUT telemetry.
# ==============================================================================

include(apex/All)
apex_module(apex_horizon_demo_world_query_probe)

apex_add_interface_library(
  NAME
  ${LIB_NAME}
  INC
  "${INC_DIR}"
  DEPS_INTERFACE
  sim_environment_celestial_body
  system_core_system_component
  utilities_helpers
  utilities_math_vecmat
  fmt::fmt
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs()
