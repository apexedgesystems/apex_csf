/**
 * @file PluginNoDestroy.cpp
 * @brief A plugin library that passes the ABI gate and has no destroy factory.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/PluginInterface.hpp"

#include <cstdint>

extern "C" std::uint32_t apex_plugin_abi_version() { return APEX_PLUGIN_ABI_VERSION; }

extern "C" system_core::system_component::SystemComponentBase* apex_create_component() {
  return nullptr;
}
