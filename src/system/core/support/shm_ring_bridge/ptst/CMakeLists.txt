# ==============================================================================
# Performance Tests: system_core_support_shm_ring_bridge
#
# Benchmarks the per-tick bridgeStep push path (memcpy + cursor store + sem_post).
# ==============================================================================

include(apex/All)

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ShmRingBridge_pTest.cpp")

apex_add_ptest(
  TARGET SystemCoreSupportShmRingBridge_PTEST SOURCES ${PERF_TEST_SOURCES} LINK
  system_core_support_shm_ring_bridge
)
