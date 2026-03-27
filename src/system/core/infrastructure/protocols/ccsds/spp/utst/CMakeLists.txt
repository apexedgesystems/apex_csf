# ==============================================================================
# Unit Tests: CCSDS SPP
#
# Tests CCSDS Space Packet Protocol operations including:
#   - Immutable packer (SppMsg, SppPrimaryHeader, packPacket)
#   - Zero-copy viewer (PacketViewer, PrimaryHeaderView, peekAPID)
#   - Mutable typed messages (MutableSppMessageT, factories)
#   - Streaming processor (PD+7 extraction, resync, chunking)
#   - Time code parsing (CUC, CDS formats)
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/CcsdsSppMutableMessage_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CcsdsSppViewer_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CcsdsSppTimeCode_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CcsdsSppMessagePacker_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CcsdsSppProcessor_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreProtocolsCcsdsSpp
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_protocols_ccsds_spp
  LABELS
  system
  protocols
  ccsds
  spp
)
