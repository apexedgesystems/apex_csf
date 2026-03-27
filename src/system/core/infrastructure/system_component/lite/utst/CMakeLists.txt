# ==============================================================================
# Unit Tests: system_component_lite
#
# Tests LiteComponentBase lifecycle (init/reset/re-init), registration with
# fullUid computation, idempotent init, and IComponent interface compliance.
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/LiteComponentBase_uTest.cpp")

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemComponentLite
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_component_lite
  LABELS
  system
  system_component
  lite
)
