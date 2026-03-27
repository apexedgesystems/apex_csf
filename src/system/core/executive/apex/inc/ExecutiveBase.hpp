#ifndef APEX_SYSTEM_CORE_EXECUTIVE_BASE_HPP
#define APEX_SYSTEM_CORE_EXECUTIVE_BASE_HPP
/**
 * @file ExecutiveBase.hpp
 * @brief Base for executives (status, init(), run()), sharing one SystemLog across the system.
 *
 * Notes:
 * - Inherits status/init/logger behavior from SystemComponentBase.
 * - Derived executives create the single SystemLog and install it into the base.
 * - init() may perform I/O; not real-time safe.
 */

#include "src/system/core/executive/base/inc/IExecutive.hpp"
#include "src/system/core/infrastructure/system_component/apex/inc/SystemComponentBase.hpp"

#include <cstdint>

namespace executive {

/**
 * @class ExecutiveBase
 * @brief Minimal executive interface layered on SystemComponentBase.
 *
 * Status handling mirrors other components:
 * - SUCCESS = 0 (via SystemComponentBase::Status)
 * - status() returns uint8_t; cast to/from Status where needed.
 */
class ExecutiveBase : public system_core::system_component::SystemComponentBase, public IExecutive {
public:
  using Status = system_core::system_component::Status;

  /* ----------------------------- Component Identity ----------------------------- */

  /// Component type identifier (0 = Executive, the root component).
  static constexpr std::uint16_t COMPONENT_ID = 0;

  /// Component name for collision detection.
  static constexpr const char* COMPONENT_NAME = "Executive";

  /** @brief Get component type identifier. */
  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }

  /** @brief Get component name. */
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }

  /** @brief Get component type classification. */
  [[nodiscard]] system_core::system_component::ComponentType
  componentType() const noexcept override {
    return system_core::system_component::ComponentType::EXECUTIVE;
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

  /** @brief Component label. */
  [[nodiscard]] const char* label() const noexcept override { return "EXECUTIVE"; }

protected:
  /** @brief Default constructor. */
  ExecutiveBase() noexcept { setConfigured(true); }

  /** @brief Boot-time setup hook (not RT-safe). Called by base init(). */
  [[nodiscard]] uint8_t doInit() noexcept override = 0;
};

} // namespace executive

#endif // APEX_SYSTEM_CORE_EXECUTIVE_BASE_HPP
