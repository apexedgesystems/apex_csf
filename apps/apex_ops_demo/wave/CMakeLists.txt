# ==============================================================================
# Wave generator library (header-only)
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_module(apex_ops_demo_wave)

add_library(${LIB_NAME} INTERFACE)

target_include_directories(
  ${LIB_NAME} INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
                        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

target_link_libraries(
  ${LIB_NAME}
  INTERFACE system_core_system_component utilities_helpers fmt::fmt
)

# ------------------------------------------------------------------------------
# Opt-ins
# ------------------------------------------------------------------------------

apex_add_data_manifest(MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/wave_data.toml")
