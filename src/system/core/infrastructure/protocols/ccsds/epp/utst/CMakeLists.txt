# ==============================================================================
# Unit Tests: CCSDS EPP
#
# Tests CCSDS Encapsulation Packet Protocol operations including:
#   - Immutable packer (EppHeader, EppMsg, packPacket)
#   - Zero-copy viewer (PacketViewer, EppHeaderView, peekProtocolId)
#   - Mutable typed messages (MutableEppMessageT, factories)
#   - Streaming processor (LoL-based extraction, resync, chunking)
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/CcsdsEppViewer_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CcsdsEppProcessor_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CcsdsEppMessagePacker_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CcsdsEppMutableMessage_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreProtocolsCcsdsEpp
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_protocols_ccsds_epp
  LABELS
  system
  protocols
  ccsds
  epp
)
