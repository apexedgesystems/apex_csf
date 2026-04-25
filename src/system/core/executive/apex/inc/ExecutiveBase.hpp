#ifndef APEX_SYSTEM_CORE_EXECUTIVE_BASE_HPP
#define APEX_SYSTEM_CORE_EXECUTIVE_BASE_HPP
/**
 * @file ExecutiveBase.hpp
 * @brief POSIX-tier executive base layered on SystemComponentBase + ExecutiveCore.
 *
 * Notes:
 * - SystemComponentBase provides ComponentCore identity + POSIX component
 *   features (TPRM, internal bus, command handling, logging).
 * - ExecutiveCore provides the IExecutive contract and the canonical
 *   executive identity constants (id=0, name="Executive", type=EXECUTIVE).
 * - The two bases meet here; identity overrides return the ExecutiveCore
 *   constants so the component-side identity matches the executive-side.
 * - Derived executives create the single SystemLog and install it into the
 *   base; init() may perform I/O and is not real-time safe.
 */

#include "src/system/core/executive/core/inc/ExecutiveCore.hpp"
#include "src/system/core/infrastructure/system_component/apex/inc/SystemComponentBase.hpp"

#include <cstdint>

namespace executive {

/**
 * @class ExecutiveBase
 * @brief POSIX executive base mixing SystemComponentBase and ExecutiveCore.
 *
 * Status handling mirrors other components:
 * - SUCCESS = 0 (via SystemComponentBase::Status)
 * - status() returns uint8_t; cast to/from Status where needed.
 */
class ExecutiveBase : public system_core::system_component::SystemComponentBase,
                      public ExecutiveCore {
public:
  using Status = system_core::system_component::Status;

  /* ----------------------------- Component Identity ----------------------------- */

  /** @brief Get component type identifier (0 = Executive). */
  [[nodiscard]] std::uint16_t componentId() const noexcept override {
    return ExecutiveCore::COMPONENT_ID;
  }

  /** @brief Get component name. */
  [[nodiscard]] const char* componentName() const noexcept override {
    return ExecutiveCore::COMPONENT_NAME;
  }

  /** @brief Get component type classification. */
  [[nodiscard]] system_core::system_component::ComponentType
  componentType() const noexcept override {
    return ExecutiveCore::COMPONENT_TYPE;
  }

  /** @brief Component label. */
  [[nodiscard]] const char* label() const noexcept override {
    return ExecutiveCore::COMPONENT_LABEL;
  }

  /* ----------------------------- Lifecycle ----------------------------- */

  ~ExecutiveBase() override = default;

  /** @brief Main control/scheduler loop entry. */
  [[nodiscard]] RunResult run() noexcept override = 0;

  /** @brief Request graceful shutdown. */
  void shutdown() noexcept override = 0;

  /** @brief Check if shutdown has been requested. */
  [[nodiscard]] bool isShutdownRequested() const noexcept override = 0;

  /** @brief Get number of completed execution cycles. */
  [[nodiscard]] uint64_t cycleCount() const noexcept override = 0;

protected:
  /** @brief Default constructor. */
  ExecutiveBase() noexcept { setConfigured(true); }

  /** @brief Boot-time setup hook (not RT-safe). Called by base init(). */
  [[nodiscard]] uint8_t doInit() noexcept override = 0;
};

} // namespace executive

#endif // APEX_SYSTEM_CORE_EXECUTIVE_BASE_HPP
