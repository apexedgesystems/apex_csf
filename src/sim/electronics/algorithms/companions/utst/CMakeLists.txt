# ==============================================================================
# Tests: sim_electronics_algorithms_companions
# ==============================================================================

include(apex/All)

set(TEST_SOURCES CompanionModels_uTest.cpp CompanionSet_uTest.cpp)

apex_add_gtest(
  TARGET
  TestSimElectronicsAlgorithmsCompanions
  SOURCES
  ${TEST_SOURCES}
  LINK
  sim_electronics_algorithms_companions
  LABELS
  sim
  electronics
  algorithms
  companions
)
