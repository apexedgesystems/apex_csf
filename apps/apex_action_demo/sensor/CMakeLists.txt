# ==============================================================================
# Sensor model library (header-only)
#
# Temperature ramp model with overtemp detection and endianness proxy.
# ==============================================================================

include(apex/All)
apex_module(apex_action_demo_sensor)

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
  utilities_data_proxy
  utilities_helpers
  fmt::fmt
)

# ------------------------------------------------------------------------------
# Opt-ins
# ------------------------------------------------------------------------------

apex_add_data_manifest(MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/sensor_data.toml")
