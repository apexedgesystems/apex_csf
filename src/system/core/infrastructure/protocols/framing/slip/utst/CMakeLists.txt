# ==============================================================================
# Unit Tests: SLIP Framing
#
# Tests SLIP encoding and decoding operations including:
#   - Delimiter handling
#   - Escape sequence processing
#   - Streaming decode with chunk reassembly
#   - Error handling and sync recovery
#   - Optimization path verification
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/SLIPFraming_uTest.cpp"
                      "${CMAKE_CURRENT_SOURCE_DIR}/SlipCodec_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreProtocolsFramingSlip
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_protocols_framing_slip
  LABELS
  system
  protocols
  framing
  slip
)
