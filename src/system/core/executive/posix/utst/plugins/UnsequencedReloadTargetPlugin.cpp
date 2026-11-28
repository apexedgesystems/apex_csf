/**
 * @file UnsequencedReloadTargetPlugin.cpp
 * @brief A plugin library whose component does not sequence the task the linked
 *        ReloadTarget sequences.
 */

#include "src/system/core/executive/posix/utst/plugins/ReloadTarget.hpp"

#include "src/system/core/infrastructure/system_component/posix/inc/PluginInterface.hpp"

APEX_PLUGIN_FACTORY(executive::test::UnsequencedReloadTarget)
