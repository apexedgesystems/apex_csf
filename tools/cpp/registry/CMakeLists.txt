# ==============================================================================
# Tools: system_core_registry
#
# CLI tools for inspecting and manipulating registry database files.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Registry Tools (1)
# ------------------------------------------------------------------------------

apex_add_tool(
  NAME
  registry-info
  SRC
  "${CMAKE_CURRENT_SOURCE_DIR}/registry-info.cpp"
  LINK
  system_core_registry
  fmt::fmt
)
