# ==============================================================================
# Unit tests: spec demo limits component (the spec-generated component base)
# ==============================================================================

include(apex/All)

apex_add_gtest(
  TARGET
  TestSpecDemoLimits
  SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/SpecLimits_uTest.cpp"
  LINK
  apex_spec_demo_limits
  system_core_system_component
  LABELS
  apex_spec_demo
  limits
)
