# ==============================================================================
# Module: sim_environment_celestial_body
#
# Apex SwModelBase-derived component representing one celestial body in a
# simulation (Earth, Moon, Sun, asteroid, procedural fictional planet, ...).
# Wraps the per-body environment model bundle (gravity + terrain +
# atmosphere) built by sim_environment_factory and exposes them via the
# apex data registry for other components to query.
#
# Passive component: no scheduled tasks. Active components (e.g. a future
# Propagator) query CelestialBody on-demand for gravity acceleration,
# terrain elevation, atmosphere density at the spacecraft position.
#
# Library:
#   sim_environment_celestial_body  - CPU implementation, depends on environment factory
#                         and the apex SystemComponent / SwModelBase chain.
# ==============================================================================

include(apex/All)
apex_module(sim_environment_celestial_body)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/CelestialBody.cpp")

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
  sim_environment_factory
  system_core_system_component
  utilities_helpers
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
