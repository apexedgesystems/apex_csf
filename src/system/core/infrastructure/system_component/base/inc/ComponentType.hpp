#ifndef APEX_SYSTEM_CORE_BASE_COMPONENT_TYPE_HPP
#define APEX_SYSTEM_CORE_BASE_COMPONENT_TYPE_HPP
/**
 * @file ComponentType.hpp
 * @brief Component type classification for system components.
 *
 * Part of the base interface layer - no heavy dependencies.
 * Used by IComponent and all derived implementations.
 *
 * All functions are RT-safe: O(1), no allocation, noexcept.
 */

#include <stdint.h>

namespace system_core {
namespace system_component {

/* ----------------------------- ComponentType ----------------------------- */

/**
 * @enum ComponentType
 * @brief Classification for system components.
 *
 * Each type implies different management patterns:
 *   - EXECUTIVE: The root executive component (componentId=0)
 *   - CORE: Core infrastructure (scheduler, filesystem, interface)
 *   - SW_MODEL: Software/environment models (gravity, atmosphere, terrain)
 *   - HW_MODEL: Hardware emulation models (VCanBus, VSerial, IMU emulator)
 *   - SUPPORT: Runtime support services (health, fault injection)
 *   - DRIVER: Real hardware interfaces (CAN, serial, IMU)
 *
 * Schedulable types: SW_MODEL, HW_MODEL, SUPPORT, DRIVER
 * Non-schedulable types: EXECUTIVE, CORE
 *
 * Used by registry for organization and by executive for logging decisions.
 */
enum class ComponentType : uint8_t {
  EXECUTIVE = 0, ///< Root executive component (singleton).
  CORE = 1,      ///< Core infrastructure (scheduler, filesystem).
  SW_MODEL = 2,  ///< Software/environment simulation models.
  HW_MODEL = 3,  ///< Hardware emulation models.
  SUPPORT = 4,   ///< Runtime support services.
  DRIVER = 5     ///< Real hardware interfaces.
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Human-readable string for ComponentType.
 * @param type Component type value.
 * @return Static string (no allocation).
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline const char* toString(ComponentType type) noexcept {
  switch (type) {
  case ComponentType::EXECUTIVE:
    return "EXECUTIVE";
  case ComponentType::CORE:
    return "CORE";
  case ComponentType::SW_MODEL:
    return "SW_MODEL";
  case ComponentType::HW_MODEL:
    return "HW_MODEL";
  case ComponentType::SUPPORT:
    return "SUPPORT";
  case ComponentType::DRIVER:
    return "DRIVER";
  }
  return "UNKNOWN";
}

/**
 * @brief Get log subdirectory name for component type.
 * @param type Component type value.
 * @return Directory name (e.g., "core", "models").
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline const char* logSubdir(ComponentType type) noexcept {
  switch (type) {
  case ComponentType::EXECUTIVE:
    return "core";
  case ComponentType::CORE:
    return "core";
  case ComponentType::SW_MODEL:
    return "models";
  case ComponentType::HW_MODEL:
    return "models";
  case ComponentType::SUPPORT:
    return "support";
  case ComponentType::DRIVER:
    return "drivers";
  }
  return "core";
}

/**
 * @brief Check if component type is a simulation model.
 * @param type Component type value.
 * @return true if SW_MODEL or HW_MODEL.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool isModel(ComponentType type) noexcept {
  return type == ComponentType::SW_MODEL || type == ComponentType::HW_MODEL;
}

/**
 * @brief Check if component type is core infrastructure.
 * @param type Component type value.
 * @return true if EXECUTIVE or CORE.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool isCoreInfra(ComponentType type) noexcept {
  return type == ComponentType::EXECUTIVE || type == ComponentType::CORE;
}

/**
 * @brief Check if component type is schedulable (can have tasks).
 * @param type Component type value.
 * @return true if SW_MODEL, HW_MODEL, SUPPORT, or DRIVER.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool isSchedulable(ComponentType type) noexcept {
  return type == ComponentType::SW_MODEL || type == ComponentType::HW_MODEL ||
         type == ComponentType::SUPPORT || type == ComponentType::DRIVER;
}

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_CORE_BASE_COMPONENT_TYPE_HPP
