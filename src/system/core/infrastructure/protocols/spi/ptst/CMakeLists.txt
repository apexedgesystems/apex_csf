# ==============================================================================
# Performance Tests: system_core_protocols_spi
#
# SPI full-duplex transfer, write/read throughput, and stats overhead.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/SpiSocketDevice_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET SpiSocketDevice_PTEST SOURCES ${PERF_TEST_SOURCES} LINK system_core_protocols_spi
)
