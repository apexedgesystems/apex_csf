# ==============================================================================
# Unit Tests: sim_electronics_chips_intel4004_behavioral
#
# Behavioral CPU validation: 46 opcodes, register file, stack, carry flag.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (2 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Intel4004Cpu_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Intel4004Disassembler_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSimElectronicsIntel4004Behavioral
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_electronics_chips_intel4004_behavioral
  fmt::fmt
  LABELS
  sim
  electronics
  intel4004
)
