# ==============================================================================
# Tests: system_core_interface
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Test Sources (3 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/ApexInterface_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/BufferPool_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/FileTransferHandler_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreInterface
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_interface
  system_core_protocols_framing_cobs
  system_core_protocols_framing_slip
  system_core_protocols_network_tcp
  system_core_protocols_network_udp
  system_core_logs
  fmt::fmt
  LABELS
  system
  core
  interface
)
