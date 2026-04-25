# ==============================================================================
# Unit Tests: system_component_mcu
#
# Tests McuComponentBase lifecycle (init/reset/re-init), registration with
# fullUid computation, idempotent init, and IComponent interface compliance.
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/McuComponentBase_uTest.cpp")

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemComponentMcu
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_component_mcu
  LABELS
  system
  system_component
  mcu
)
