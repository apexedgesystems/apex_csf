/**
 * @file PluginLoader.hpp
 * @brief Loads and manages a single component plugin (.so) via dlopen.
 *
 * Lifecycle:
 *   1. load(path) — dlopen, resolve factory symbols, create component
 *   2. component() — access the live instance
 *   3. unload() — destroy component, dlclose
 *   4. reload(path) — unload + load (caller must re-register)
 *
 * Thread safety: NOT thread-safe. Caller must ensure component is locked
 * and not executing during load/unload/reload.
 *
 * @note NOT RT-safe: Control-plane only (dlopen/dlclose allocate).
 */

#ifndef APEX_SYSTEM_CORE_PLUGIN_LOADER_HPP
#define APEX_SYSTEM_CORE_PLUGIN_LOADER_HPP

#include "src/system/core/infrastructure/system_component/posix/inc/PluginInterface.hpp"

#include <cstdint>
#include <filesystem>

namespace system_core {
namespace system_component {

/* ----------------------------- PluginLoader ----------------------------- */

class PluginLoader {
public:
  PluginLoader() = default;
  ~PluginLoader() { unload(); }

  // Non-copyable, movable.
  PluginLoader(const PluginLoader&) = delete;
  PluginLoader& operator=(const PluginLoader&) = delete;
  PluginLoader(PluginLoader&& other) noexcept;
  PluginLoader& operator=(PluginLoader&& other) noexcept;

  /**
   * @brief Load a component plugin from a shared library.
   * @param soPath Path to the .so file.
   * @return 0 on success, nonzero error code on failure.
   *
   * Error codes:
   *   17 — dlopen failed
   *   18 — factory symbol not found
   *   19 — component creation failed
   */
  [[nodiscard]] std::uint8_t load(const std::filesystem::path& soPath) noexcept;

  /**
   * @brief Unload the current plugin (destroy component + dlclose).
   * @note Safe to call when nothing is loaded.
   */
  void unload() noexcept;

  /**
   * @brief Unload current plugin and load a new one.
   * @param soPath Path to the new .so file.
   * @return 0 on success, nonzero error code on failure.
   */
  [[nodiscard]] std::uint8_t reload(const std::filesystem::path& soPath) noexcept;

  /// Get the live component instance (nullptr if not loaded).
  [[nodiscard]] SystemComponentBase* component() noexcept { return component_; }
  [[nodiscard]] const SystemComponentBase* component() const noexcept { return component_; }

  /// Check if a plugin is currently loaded.
  [[nodiscard]] bool isLoaded() const noexcept { return handle_ != nullptr; }

  /// Get the path of the currently loaded .so.
  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  /// Get the last error message (from dlerror or internal).
  [[nodiscard]] const char* lastError() const noexcept { return lastError_; }

private:
  void* handle_{nullptr};
  ApexPluginCreateFn createFn_{nullptr};
  ApexPluginDestroyFn destroyFn_{nullptr};
  SystemComponentBase* component_{nullptr};
  std::filesystem::path path_;
  const char* lastError_{nullptr};
};

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_CORE_PLUGIN_LOADER_HPP
