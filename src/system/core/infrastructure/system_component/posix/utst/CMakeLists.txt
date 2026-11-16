# ==============================================================================
# Unit Tests: system_core_system_component
#
# Tests Status enum, SystemComponentBase lifecycle (init/reset/idempotent)
# and component log, the schedulable base's task tables across reset, the
# hardware model's transport link, the registry's instance limit,
# PluginLoader's refusals and error text,
# ParamBank<T> A/B parameter staging (load/apply/rollback),
# PackedTprm archive extraction and entry lookup, data primitives
# (DataCategory, ModelData, DataTarget), and byte-level conformance against
# the committed TPRM golden vectors shared with the rust tooling.
# ==============================================================================

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/SystemComponentStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SystemComponentBase_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SchedulableComponentBase_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ParamBank_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ComponentRegistry_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PluginLoader_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PackedTprm_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/GoldenVectors_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TprmReadback_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TprmPayload_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/DataCategory_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/DataTarget_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ModelData_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Test Plugins
# ------------------------------------------------------------------------------

# Plugin libraries the loader tests open. Each passes the ABI gate and lacks
# one factory symbol.
foreach (_plugin NoCreate NoDestroy)
  apex_add_test_plugin(
    TARGET SystemComponentTestPlugin_${_plugin} SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/Plugin${_plugin}.cpp"
  )
endforeach ()

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

# The golden vectors are the cross-language contract (rust generates them,
# this suite consumes them); the path is baked so ctest runs from any cwd.
apex_add_gtest(
  TARGET
  TestSystemCoreSystemComponent
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  system_core_system_component
  system_component_mcu
  system_core_logs
  utilities_helpers
  fmt::fmt
  DEFS
  APEX_TPRM_VECTORS_DIR="${CMAKE_SOURCE_DIR}/compat/tprm"
  PLUGINS
  SystemComponentTestPlugin_NoCreate
  SystemComponentTestPlugin_NoDestroy
  LABELS
  system
  system_component
)
