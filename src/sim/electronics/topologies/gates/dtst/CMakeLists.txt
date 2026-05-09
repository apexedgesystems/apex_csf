# ==============================================================================
# Development Tests: sim_electronics_topologies_gates
#
# Cross-validation of gate circuit models against ngspice.
# ==============================================================================

include(apex/All)

set(DTEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/CmosInverterVsNgspice_dTest.cpp")

apex_add_gtest(
  TARGET sim_electronics_topologies_gates_ngspice_dTest
  SOURCES ${DTEST_SOURCES}
  LINK sim_electronics_topologies_gates sim_electronics_algorithms_spice_ngspice fmt::fmt
  LABELS electronics gates ngspice
)
