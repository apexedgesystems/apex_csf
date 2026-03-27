# ==============================================================================
# Unit Tests: Protocol Common Utilities
#
# Tests ByteTrace mixin and format helpers including:
#   - TraceDirection enum and toString
#   - Attach/detach/enable lifecycle
#   - Trace invocation (enabled, disabled, detached)
#   - formatBytesHex formatting and truncation
#   - formatTraceMessage output format
#   - Null pointer and edge case safety
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ByteTrace_uTest.cpp")

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSystemCoreProtocolsCommon
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_protocols_common
  LABELS
  system
  protocols
  common
)
