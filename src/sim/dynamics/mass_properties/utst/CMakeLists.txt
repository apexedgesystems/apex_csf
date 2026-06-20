# ==============================================================================
# Unit Tests: sim_dynamics_mass_properties
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/FuelBurnMassProperties_uTest.cpp"
                      "${CMAKE_CURRENT_SOURCE_DIR}/MassProperties_uTest.cpp"
)

apex_add_gtest(
  TARGET
  TestSimDynamicsMassProperties
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_dynamics_mass_properties
  LABELS
  sim
  dynamics
  mass_properties
)
