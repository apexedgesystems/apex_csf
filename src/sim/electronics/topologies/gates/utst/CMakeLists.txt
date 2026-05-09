# ==============================================================================
# Unit Tests: sim_electronics_topologies_gates
#
# Truth table validation for all gates and adders, plus circuit-level CMOS
# gate models for all 7 gate types (NOT, NAND, NOR, AND, OR, XOR, XNOR)
# solved through the MNA infrastructure with MosfetLevel1 physics.
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/LogicGates_uTest.cpp"
                      "${CMAKE_CURRENT_SOURCE_DIR}/CmosGateCircuits_uTest.cpp"
)

apex_add_gtest(
  TARGET
  TestSimElectronicsGates
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_electronics_topologies_gates
  sim_electronics_devices_composite
  sim_electronics_circuit
  COVERAGE_FOR
  sim_electronics_topologies_gates
  LABELS
  sim
  electronics
  digital
)
