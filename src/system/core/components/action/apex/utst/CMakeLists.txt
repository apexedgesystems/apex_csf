# ==============================================================================
# Unit Tests: system_core_action
#
# Tests ActionComponent lifecycle and the action engine primitives
# (ActionInterface, DataAction, DataWatchpoint, DataSequence,
# EventNotification, SequenceValidation).
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (9 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/ActionComponent_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ActionInterface_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/DataAction_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/DataWatchpoint_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/DataSequence_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/EventNotification_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ResourceCatalog_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SequenceValidation_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SequenceCatalog_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreAction
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_action
  fmt::fmt
  LABELS
  system
  core
  action
)
