# ==============================================================================
# Performance Tests: system_core_protocols_network_unix
#
# Unix domain socket echo latency, throughput, multi-client, and scaling.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/UnixSocket_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET UnixSocket_PTEST SOURCES ${PERF_TEST_SOURCES} LINK system_core_protocols_network_unix
)
