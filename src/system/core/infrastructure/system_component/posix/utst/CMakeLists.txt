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
# Test Target
# ------------------------------------------------------------------------------

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
  LABELS
  system
  system_component
)

# The golden vectors are the cross-language contract (rust generates them,
# this suite consumes them); the path is baked so ctest runs from any cwd.
target_compile_definitions(
  TestSystemCoreSystemComponent PRIVATE APEX_TPRM_VECTORS_DIR="${CMAKE_SOURCE_DIR}/compat/tprm"
)

# Plugin libraries the loader tests open. Each passes the ABI gate and lacks
# one factory symbol; nothing links them.
set(_test_plugin_dir "${CMAKE_BINARY_DIR}/test_plugins/system_component")
foreach (_plugin NoCreate NoDestroy)
  add_library(
    SystemComponentTestPlugin_${_plugin} MODULE
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/Plugin${_plugin}.cpp"
  )
  target_include_directories(SystemComponentTestPlugin_${_plugin} PRIVATE "${CMAKE_SOURCE_DIR}")
  set_target_properties(
    SystemComponentTestPlugin_${_plugin}
    PROPERTIES PREFIX ""
               SUFFIX ".so"
               LIBRARY_OUTPUT_DIRECTORY "${_test_plugin_dir}"
  )
  add_dependencies(TestSystemCoreSystemComponent SystemComponentTestPlugin_${_plugin})
endforeach ()
target_compile_definitions(
  TestSystemCoreSystemComponent PRIVATE APEX_TEST_PLUGIN_DIR="${_test_plugin_dir}"
)
