# ==============================================================================
# Tool: registry-info
#
# Inspects registry database files; installs to bin/tools/analysis.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# registry-info
# ------------------------------------------------------------------------------

apex_add_tool(
  NAME
  registry-info
  SRC
  "${CMAKE_CURRENT_SOURCE_DIR}/src/registry-info.cpp"
  LINK
  system_core_registry
  fmt::fmt
)
