# ==============================================================================
# Unit Tests: system_core_support_shm_ring_bridge
# ==============================================================================

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ShmRingBridge_uTest.cpp")

apex_add_gtest(
  TARGET
  TestSystemCoreSupportShmRingBridge
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_support_shm_ring_bridge
  fmt::fmt
  LABELS
  system
  support
  shm_ring_bridge
)
