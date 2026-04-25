# ==============================================================================
# Unit Tests: executive_core
#
# Tests ExecutiveCore identity constants (id, name, type, label) and
# IExecutive contract enforcement on a minimal concrete subclass.
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ExecutiveCore_uTest.cpp")

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestExecutiveCore
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  executive_core
  LABELS
  system
  executive
  core
)
