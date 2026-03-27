# ==============================================================================
# Unit Tests: COBS Framing
#
# Tests COBS encoding and decoding operations including:
#   - Delimiter handling
#   - Zero byte processing and run length encoding
#   - Streaming decode with chunk reassembly
#   - Error handling and sync recovery
#   - Optimization path verification
#   - Codec wrapper streaming accumulation
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/COBSFraming_uTest.cpp"
                      "${CMAKE_CURRENT_SOURCE_DIR}/CobsCodec_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreProtocolsFramingCobs
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_protocols_framing_cobs
  LABELS
  system
  protocols
  framing
  cobs
)
