# ==============================================================================
# Unit Tests: system_component_base
#
# Tests IComponent interface contract, ComponentType enum, and base type
# utilities (toString, classification predicates).
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/IComponent_uTest.cpp")

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  SystemComponentBase_uTest
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_component_base
  LABELS
  system
  system_component
)
