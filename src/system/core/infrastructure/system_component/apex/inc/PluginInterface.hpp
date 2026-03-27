/**
 * @file PluginInterface.hpp
 * @brief C factory interface for dynamically loadable component plugins.
 *
 * A plugin .so must export these two symbols:
 *   - apex_create_component()  — allocate and return a new component instance
 *   - apex_destroy_component() — destroy a previously created instance
 *
 * Example plugin:
 * @code
 *   #include "PluginInterface.hpp"
 *   #include "MyModel.hpp"
 *
 *   APEX_PLUGIN_FACTORY(MyModel)
 * @endcode
 *
 * @note RT-safe: Factory functions are control-plane only (called during load/unload).
 */

#ifndef APEX_SYSTEM_CORE_PLUGIN_INTERFACE_HPP
#define APEX_SYSTEM_CORE_PLUGIN_INTERFACE_HPP

#include "src/system/core/infrastructure/system_component/apex/inc/SystemComponentBase.hpp"

/* ----------------------------- Constants ----------------------------- */

/// Name of the create factory function exported by plugin .so files.
static constexpr const char* APEX_PLUGIN_CREATE_SYMBOL = "apex_create_component";

/// Name of the destroy factory function exported by plugin .so files.
static constexpr const char* APEX_PLUGIN_DESTROY_SYMBOL = "apex_destroy_component";

/// Create function signature: returns heap-allocated SystemComponentBase*.
using ApexPluginCreateFn = system_core::system_component::SystemComponentBase* (*)();

/// Destroy function signature: takes pointer returned by create.
using ApexPluginDestroyFn = void (*)(system_core::system_component::SystemComponentBase*);

/* ----------------------------- API ----------------------------- */

/**
 * @brief Emit extern "C" factory functions for a component class.
 *
 * Usage: place at file scope in the plugin .cpp:
 *   APEX_PLUGIN_FACTORY(MyModelClass)
 *
 * @param CLASS_NAME Fully qualified class name (must be default-constructible).
 */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define APEX_PLUGIN_FACTORY(CLASS_NAME)                                                            \
  extern "C" system_core::system_component::SystemComponentBase* apex_create_component() {         \
    return new CLASS_NAME(); /* NOLINT(cppcoreguidelines-owning-memory) */                         \
  }                                                                                                \
  extern "C" void apex_destroy_component(                                                          \
      system_core::system_component::SystemComponentBase* comp) {                                  \
    delete comp; /* NOLINT(cppcoreguidelines-owning-memory) */                                     \
  }

#endif // APEX_SYSTEM_CORE_PLUGIN_INTERFACE_HPP
