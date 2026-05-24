# ==============================================================================
# Performance Tests: scheduler_mcu
#
# Tick overhead, rate-group decimation cost, and task-scaling benchmarks for
# the zero-allocation MCU scheduler.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/McuScheduler_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(TARGET McuScheduler_PTEST SOURCES ${PERF_TEST_SOURCES} LINK scheduler_mcu)
