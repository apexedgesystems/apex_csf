# ==============================================================================
# Unit Tests: system_core_data
#
# Tests data proxy infrastructure (ModelData, EndiannessProxy, FaultInjectionProxy,
# MasterDataProxy), runtime operations (DataTarget, DataWatchpoint, DataAction,
# DataSequence, EventNotification, ActionInterface), and sequence validation.
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources (12 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/DataCategory_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/EndiannessProxy_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/FaultInjectionProxy_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MasterDataProxy_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ModelData_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/DataTarget_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/DataAction_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/DataWatchpoint_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/DataSequence_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/EventNotification_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ActionInterface_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SequenceValidation_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreData
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_data
  fmt::fmt
  LABELS
  system
  data
)
