# ==============================================================================
# Performance Tests: system_core_protocols_wireless_bluetooth
#
# Bluetooth RFCOMM read/write throughput, round-trip latency, and overheads.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/RfcommAdapter_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET RfcommAdapter_PTEST SOURCES ${PERF_TEST_SOURCES} LINK
  system_core_protocols_wireless_bluetooth
)
