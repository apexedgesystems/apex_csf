#ifndef APEX_SYSTEM_CORE_SYSTEM_COMPONENT_COMPONENT_REGISTRY_HPP
#define APEX_SYSTEM_CORE_SYSTEM_COMPONENT_COMPONENT_REGISTRY_HPP
/**
 * @file ComponentRegistry.hpp
 * @brief Component registration helper with collision detection.
 */

#include "src/system/core/infrastructure/system_component/apex/inc/SystemComponentBase.hpp"

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <utility>

namespace system_core {
namespace system_component {

/* ----------------------------- ComponentRegistry ----------------------------- */

/**
 * @struct ComponentRegistry
 * @brief Tracks registered components with collision detection.
 *
 * Collision rules:
 *  - Same componentId + different componentName = COLLISION (error)
 *  - Same componentId + same componentName = MULTI-INSTANCE (OK)
 *
 * @note NOT RT-safe. Use only during startup/registration phase.
 */
struct ComponentRegistry {
  /// Registered component info: componentId -> (componentName, instanceCount)
  std::unordered_map<std::uint16_t, std::pair<const char*, std::uint8_t>> registered;

  /**
   * @brief Register a component and get its instance index.
   * @param component Pointer to component.
   * @param[out] instanceIndex Assigned instance index.
   * @return true on success, false on collision.
   * @note NOT RT-safe.
   */
  bool registerComponent(SystemComponentBase* component, std::uint8_t& instanceIndex) {
    const std::uint16_t ID = component->componentId();
    const char* NAME = component->componentName();

    auto it = registered.find(ID);
    if (it != registered.end()) {
      // Same ID exists - check for collision
      const char* existingName = it->second.first;
      if (std::strcmp(existingName, NAME) != 0) {
        // COLLISION: same ID, different name
        return false;
      }
      // MULTI-INSTANCE: same ID, same name - increment instance count
      instanceIndex = it->second.second++;
    } else {
      // New component type - register as instance 0
      instanceIndex = 0;
      registered[ID] = {NAME, 1};
    }

    component->setInstanceIndex(instanceIndex);
    return true;
  }
};

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SYSTEM_COMPONENT_COMPONENT_REGISTRY_HPP
