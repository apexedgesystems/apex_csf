/**
 * @file RenamedReloadTargetPlugin.cpp
 * @brief A plugin library whose component has the linked ReloadTarget's id
 *        and another name.
 */

#include "src/system/core/executive/posix/utst/plugins/ReloadTarget.hpp"

#include "src/system/core/infrastructure/system_component/posix/inc/PluginInterface.hpp"

APEX_PLUGIN_FACTORY(executive::test::RenamedReloadTarget)
