# ==============================================================================
# Performance Tests: system_core_protocols_fieldbus_lin
#
# LIN frame build/parse, PID/checksum, and PTY-loopback transaction latency.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/LinController_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET LinController_PTEST SOURCES ${PERF_TEST_SOURCES} LINK system_core_protocols_fieldbus_lin
)
