# ==============================================================================
# Wave generator library (header-only)
#
# Configurable waveform generator with telemetry output.
# ==============================================================================

include(apex/All)
apex_module(apex_ops_demo_wave)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(
  NAME
  ${LIB_NAME}
  INC
  "${INC_DIR}"
  DEPS_INTERFACE
  system_core_system_component
  utilities_helpers
  fmt::fmt
)

# ------------------------------------------------------------------------------
# Opt-ins
# ------------------------------------------------------------------------------

apex_add_data_manifest(MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/wave_data.toml")
