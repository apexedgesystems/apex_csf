/**
 * @file PluginLoader.cpp
 * @brief Implementation of PluginLoader (dlopen/dlclose wrapper).
 */

#include "src/system/core/infrastructure/system_component/apex/inc/PluginLoader.hpp"

#include <dlfcn.h>

namespace system_core {
namespace system_component {

/* ----------------------------- PluginLoader Methods ----------------------------- */

PluginLoader::PluginLoader(PluginLoader&& other) noexcept
    : handle_(other.handle_), createFn_(other.createFn_), destroyFn_(other.destroyFn_),
      component_(other.component_), path_(std::move(other.path_)), lastError_(other.lastError_) {
  other.handle_ = nullptr;
  other.createFn_ = nullptr;
  other.destroyFn_ = nullptr;
  other.component_ = nullptr;
  other.lastError_ = nullptr;
}

PluginLoader& PluginLoader::operator=(PluginLoader&& other) noexcept {
  if (this != &other) {
    unload();
    handle_ = other.handle_;
    createFn_ = other.createFn_;
    destroyFn_ = other.destroyFn_;
    component_ = other.component_;
    path_ = std::move(other.path_);
    lastError_ = other.lastError_;
    other.handle_ = nullptr;
    other.createFn_ = nullptr;
    other.destroyFn_ = nullptr;
    other.component_ = nullptr;
    other.lastError_ = nullptr;
  }
  return *this;
}

std::uint8_t PluginLoader::load(const std::filesystem::path& soPath) noexcept {
  // Unload any existing plugin first.
  unload();

  // dlopen with RTLD_NOW to resolve all symbols immediately.
  handle_ = ::dlopen(soPath.c_str(), RTLD_NOW);
  if (handle_ == nullptr) {
    lastError_ = ::dlerror();
    return 17; // DLOPEN_FAILED
  }

  // Resolve create factory.
  void* createSym = ::dlsym(handle_, APEX_PLUGIN_CREATE_SYMBOL);
  if (createSym == nullptr) {
    lastError_ = ::dlerror();
    ::dlclose(handle_);
    handle_ = nullptr;
    return 18; // FACTORY_NOT_FOUND
  }

  // Resolve destroy factory.
  void* destroySym = ::dlsym(handle_, APEX_PLUGIN_DESTROY_SYMBOL);
  if (destroySym == nullptr) {
    lastError_ = ::dlerror();
    ::dlclose(handle_);
    handle_ = nullptr;
    return 18; // FACTORY_NOT_FOUND
  }

  // Cast to typed function pointers.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  createFn_ = reinterpret_cast<ApexPluginCreateFn>(createSym);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  destroyFn_ = reinterpret_cast<ApexPluginDestroyFn>(destroySym);

  // Create the component instance.
  component_ = createFn_();
  if (component_ == nullptr) {
    lastError_ = "apex_create_component() returned nullptr";
    ::dlclose(handle_);
    handle_ = nullptr;
    createFn_ = nullptr;
    destroyFn_ = nullptr;
    return 19; // INIT_FAILED
  }

  path_ = soPath;
  lastError_ = nullptr;
  return 0;
}

void PluginLoader::unload() noexcept {
  if (component_ != nullptr && destroyFn_ != nullptr) {
    destroyFn_(component_);
  }
  component_ = nullptr;
  createFn_ = nullptr;
  destroyFn_ = nullptr;

  if (handle_ != nullptr) {
    ::dlclose(handle_);
    handle_ = nullptr;
  }

  path_.clear();
}

std::uint8_t PluginLoader::reload(const std::filesystem::path& soPath) noexcept {
  unload();
  return load(soPath);
}

} // namespace system_component
} // namespace system_core
