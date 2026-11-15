/**
 * @file ReloadTargetPlugin.cpp
 * @brief The plugin library RELOAD_LIBRARY swaps in for the linked ReloadTarget.
 */

#include "src/system/core/executive/posix/utst/plugins/ReloadTarget.hpp"

#include "src/system/core/infrastructure/system_component/posix/inc/PluginInterface.hpp"

APEX_PLUGIN_FACTORY(executive::test::ReloadTarget)
