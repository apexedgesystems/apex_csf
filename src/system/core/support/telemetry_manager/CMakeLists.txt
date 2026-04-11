# ==============================================================================
# system_core_support_telemetry_manager - Push telemetry for C2 ground systems
#
# Reads registered data blocks from the registry and pushes them via
# postInternalTelemetry() at configured rates. Enables ground systems to
# receive telemetry without polling.
#
# Header-only: No .cpp sources (all logic in TelemetryManager.hpp).
# ==============================================================================

include(apex/All)
apex_module(system_core_support_telemetry_manager)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

add_library(${LIB_NAME} INTERFACE)

target_include_directories(
  ${LIB_NAME} INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
                        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

target_link_libraries(
  ${LIB_NAME}
  INTERFACE system_core_system_component system_core_registry utilities_helpers fmt::fmt
)

# ------------------------------------------------------------------------------
# Opt-ins
# ------------------------------------------------------------------------------

apex_add_data_manifest()
