#ifndef APEX_SYSTEM_COMPONENT_MCU_COMPONENT_BASE_HPP
#define APEX_SYSTEM_COMPONENT_MCU_COMPONENT_BASE_HPP
/**
 * @file McuComponentBase.hpp
 * @brief MCU-tier IComponent base layered on ComponentCore.
 *
 * McuComponentBase is the bare-metal-safe component base. It inherits the
 * shared identity / lifecycle / registration state from ComponentCore and
 * adds MCU-specific constraints (deleted moves, no allocation contracts).
 *
 * Trade-offs vs SystemComponentBase:
 *   - No TPRM file loading (use compile-time or simple binary blobs)
 *   - No data descriptors / registry integration
 *   - No internal bus / command queue
 *   - No component logging to filesystem
 *   - No task/sequence lookups
 *
 * RT Constraints:
 *   - All query methods are RT-safe (const, noexcept, O(1))
 *   - init() and reset() are boot-time only
 *   - No allocation in any method
 *
 * @note Use SystemComponentBase for full-featured Linux/RTOS deployments.
 */

#include "src/system/core/infrastructure/system_component/base/inc/ComponentType.hpp"
#include "src/system/core/infrastructure/system_component/core/inc/ComponentCore.hpp"

#include <stdint.h>

namespace system_core {
namespace system_component {
namespace mcu {

/* ----------------------------- McuComponentBase ----------------------------- */

/**
 * @class McuComponentBase
 * @brief Minimal IComponent base for MCU targets.
 *
 * Derived classes must implement (from ComponentCore / IComponent):
 *   - componentId(), componentName(), componentType(), label()
 *   - doInit()
 *
 * Optionally override:
 *   - doReset() - cleanup logic for reset() (default no-op).
 *
 * @note RT-safe queries: status(), isInitialized(), fullUid(), instanceIndex().
 * @note NOT RT-safe: init(), reset() (may configure hardware, etc.).
 */
class McuComponentBase : public ComponentCore {
public:
  /** @brief Default constructor. */
  McuComponentBase() noexcept = default;

  /** @brief Virtual destructor. */
  ~McuComponentBase() override = default;

  // Non-copyable (components have identity)
  McuComponentBase(const McuComponentBase&) = delete;
  McuComponentBase& operator=(const McuComponentBase&) = delete;

  // Non-movable (embedded components are typically static)
  McuComponentBase(McuComponentBase&&) = delete;
  McuComponentBase& operator=(McuComponentBase&&) = delete;
};

} // namespace mcu
} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_COMPONENT_MCU_COMPONENT_BASE_HPP
