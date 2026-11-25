/**
 * @file PluginOtherAbi.cpp
 * @brief A plugin library with both factories that reports another ABI version.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/PluginInterface.hpp"

#include <cstdint>

extern "C" std::uint32_t apex_plugin_abi_version() { return APEX_PLUGIN_ABI_VERSION + 1U; }

extern "C" system_core::system_component::SystemComponentBase* apex_create_component() {
  return nullptr;
}

extern "C" void apex_destroy_component(system_core::system_component::SystemComponentBase* comp) {
  (void)comp;
}
