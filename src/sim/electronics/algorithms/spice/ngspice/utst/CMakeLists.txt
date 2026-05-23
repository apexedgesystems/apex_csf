# ==============================================================================
# Unit Tests: sim_electronics_algorithms_spice_ngspice
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Test Sources
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES NgspiceWrapper_uTest.cpp)

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  sim_electronics_algorithms_spice_ngspice_uTest
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_electronics_algorithms_spice_ngspice
  fmt::fmt
  LABELS
  sim
  electronics
  spice
)
