# ==============================================================================
# Unit Tests: system_core_support_system_monitor (1 file)
#
# Tests run on the host and exercise real /proc and /sys reads.
# They verify invariants (e.g., core count > 0) rather than exact values.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/SystemMonitor_uTest.cpp")

# ------------------------------------------------------------------------------
# Test Executable
# ------------------------------------------------------------------------------

# In CUDA builds the system_monitor library pulls in seeker_gpu, which links
# NVML, so the test binary then needs the NVIDIA driver at load. Label it cuda
# in that case so the test runner can exclude it where no GPU is present.
set(_MONITOR_LABELS system support)
if (CUDAToolkit_FOUND AND TARGET seeker_gpu)
  list(APPEND _MONITOR_LABELS cuda)
endif ()

apex_add_gtest(
  TARGET
  TestSystemCoreSupport_SystemMonitor
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_support_system_monitor
  fmt::fmt
  LABELS
  ${_MONITOR_LABELS}
)
