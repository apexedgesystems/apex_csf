# ==============================================================================
# Performance Tests: system_core_protocols_serial_uart
#
# UART adapter I/O throughput, scaling, latency, and configuration overhead.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/UartAdapter_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET UartAdapter_PTEST SOURCES ${PERF_TEST_SOURCES} LINK system_core_protocols_serial_uart
)
