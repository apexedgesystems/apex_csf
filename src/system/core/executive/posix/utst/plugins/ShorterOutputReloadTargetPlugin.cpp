/**
 * @file ShorterOutputReloadTargetPlugin.cpp
 * @brief A plugin library whose component registers a shorter output block than the
 *        linked ReloadTarget.
 */

#include "src/system/core/executive/posix/utst/plugins/ReloadTarget.hpp"

#include "src/system/core/infrastructure/system_component/posix/inc/PluginInterface.hpp"

APEX_PLUGIN_FACTORY(executive::test::ShorterOutputReloadTarget)
