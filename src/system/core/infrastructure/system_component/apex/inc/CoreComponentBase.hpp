#ifndef APEX_SYSTEM_COMPONENT_CORE_COMPONENT_BASE_HPP
#define APEX_SYSTEM_COMPONENT_CORE_COMPONENT_BASE_HPP
/**
 * @file CoreComponentBase.hpp
 * @brief Base class for core infrastructure components.
 *
 * Core components are non-schedulable infrastructure that the executive
 * owns and manages directly: Scheduler, FileSystem, Registry, ApexInterface.
 *
 * Core components:
 *   - Cannot have scheduled tasks (use SystemComponentBase directly)
 *   - Return ComponentType::CORE
 *   - Are single-instance (instanceIndex always 0)
 *   - Are managed directly by the executive
 *
 * All functions are RT-safe unless noted otherwise.
 */

#include "src/system/core/infrastructure/system_component/apex/inc/SystemComponentBase.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/ComponentType.hpp"

namespace system_core {
namespace system_component {

/* ----------------------------- CoreComponentBase ----------------------------- */

/**
 * @class CoreComponentBase
 * @brief Abstract base for core infrastructure components.
 *
 * Provides explicit typing for non-schedulable infrastructure components.
 * Derived classes: Scheduler, FileSystem, Registry, ApexInterface.
 */
class CoreComponentBase : public SystemComponentBase {
public:
  /** @brief Default constructor. */
  CoreComponentBase() noexcept = default;

  /** @brief Virtual destructor. */
  ~CoreComponentBase() override = default;

  // Non-copyable, non-movable
  CoreComponentBase(const CoreComponentBase&) = delete;
  CoreComponentBase& operator=(const CoreComponentBase&) = delete;
  CoreComponentBase(CoreComponentBase&&) = delete;
  CoreComponentBase& operator=(CoreComponentBase&&) = delete;

  /**
   * @brief Get component type classification.
   * @return ComponentType::CORE for all core infrastructure.
   */
  [[nodiscard]] ComponentType componentType() const noexcept override {
    return ComponentType::CORE;
  }
};

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_COMPONENT_CORE_COMPONENT_BASE_HPP
