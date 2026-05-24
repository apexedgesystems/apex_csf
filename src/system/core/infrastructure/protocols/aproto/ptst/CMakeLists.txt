# ==============================================================================
# Performance Tests: system_core_protocols_aproto
#
# APROTO header/packet encode-decode, CRC32 throughput, ACK/NAK latency.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Aproto_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET
  Aproto_PTEST
  SOURCES
  ${PERF_TEST_SOURCES}
  LINK
  system_core_protocols_aproto
  utilities_checksums_crc
)
