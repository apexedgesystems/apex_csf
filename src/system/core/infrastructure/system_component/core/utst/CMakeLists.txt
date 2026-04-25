# ==============================================================================
# Unit Tests: system_component_core
#
# Tests ComponentCore lifecycle (init/reset/re-init), registration with
# fullUid computation, idempotent init, hook ordering (preInitCheck ->
# preInit -> doInit), and IComponent interface compliance.
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ComponentCore_uTest.cpp")

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemComponentCore
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_component_core
  LABELS
  system
  system_component
  core
)
