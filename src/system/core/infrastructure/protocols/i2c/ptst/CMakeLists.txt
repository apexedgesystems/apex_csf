# ==============================================================================
# Performance Tests: system_core_protocols_i2c
#
# I2C write/read/writeRead throughput and stats overhead via socketpair.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/I2cSocketDevice_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET I2cSocketDevice_PTEST SOURCES ${PERF_TEST_SOURCES} LINK system_core_protocols_i2c
)
