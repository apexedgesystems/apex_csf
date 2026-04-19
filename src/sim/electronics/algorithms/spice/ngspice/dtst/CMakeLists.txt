# ==============================================================================
# Development Tests: sim_electronics_algorithms_spice_ngspice
#
# Manual verification tests comparing device models against ngspice fixtures.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Test Sources
# ------------------------------------------------------------------------------

set(DEV_TEST_SOURCES MosfetLevel1Verification_Dev.cpp)

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_devtest(
  TARGET
  NgspiceVerification_Dev
  SOURCES
  ${DEV_TEST_SOURCES}
  LINK
  sim_electronics_algorithms_spice_ngspice
  sim_electronics_devices_nonlinear
  fmt::fmt
)

# Define fixture directory path
target_compile_definitions(
  NgspiceVerification_Dev PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
