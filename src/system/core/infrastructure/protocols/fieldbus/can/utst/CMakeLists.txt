# ==============================================================================
# Unit Tests: system_core_protocols_fieldbus_can
# ==============================================================================

# ------------------------------------------------------------------------------
# Unit Test Sources (12 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/CanStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CanStats_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CanConfigFixed_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CanBusAdapter_Args_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CanBusAdapter_Batch_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CanBusAdapter_Config_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CanBusAdapter_Smoke_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CanBusAdapter_Timeouts_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CanBusAdapter_Accessors_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CanEventLoop_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/VCanInterface_Accessors_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/VCanInterface_Lifecycle_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Timing Controls
# ------------------------------------------------------------------------------

unset(ENABLE_TIMING_ALL_TESTS CACHE)
option(ENABLE_TIMING_ALL_TESTS "Label all tests as 'Timing' and run serially" ON)

set(_timing_args)
if (ENABLE_TIMING_ALL_TESTS)
  list(APPEND _timing_args TIMING_ALL)
endif ()

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreProtocolsFieldbusCan
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_protocols_fieldbus_can
  fmt::fmt
  LABELS
  system
  protocols
  fieldbus
  can
  ${_timing_args}
)
