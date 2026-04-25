#ifndef APEX_EXECUTIVE_CORE_EXECUTIVE_CORE_HPP
#define APEX_EXECUTIVE_CORE_EXECUTIVE_CORE_HPP
/**
 * @file ExecutiveCore.hpp
 * @brief Shared concrete IExecutive base for all platform tiers.
 *
 * ExecutiveCore is the single point in the inheritance hierarchy that
 * establishes "this thing is an executive." Both POSIX and MCU executive
 * bases inherit from it, so any code that needs to refer to "an
 * executive" (regardless of platform) can use ExecutiveCore as the
 * canonical type.
 *
 * Tier (from project tiering policy):
 *   - Has executive identity constants (id, name, type, label)
 *   - Not instantiable: IExecutive methods (run, shutdown,
 *     isShutdownRequested, cycleCount) remain pure virtual.
 *
 * Component identity is intentionally NOT inherited here. Each platform
 * tier brings its own ComponentCore-derived component base in (POSIX:
 * SystemComponentBase, MCU: McuComponentBase). ExecutiveCore exposes
 * the shared executive identity constants that those tiers' identity
 * overrides should return.
 *
 * Design notes:
 *   - Avoids a diamond: SystemComponentBase and McuComponentBase each
 *     inherit ComponentCore directly, and ExecutiveCore does not.
 *     A platform executive base that mixes a component base with
 *     ExecutiveCore therefore has exactly one ComponentCore subobject.
 *   - Future shared executive state (cycleCount, shutdown flag) can
 *     migrate here when the platform tiers agree on a representation.
 *     Today they differ (atomic<uint64_t> vs. templated Counter), so
 *     the state stays in the derived classes.
 */

#include "src/system/core/executive/base/inc/IExecutive.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/ComponentType.hpp"

#include <stdint.h>

namespace executive {

/* ----------------------------- ExecutiveCore ----------------------------- */

/**
 * @class ExecutiveCore
 * @brief Shared executive base providing identity constants and the
 *        IExecutive contract.
 *
 * Derived classes must implement (from IExecutive):
 *   - run(), shutdown(), isShutdownRequested(), cycleCount()
 *
 * Derived classes that also inherit a ComponentCore-based component
 * base (SystemComponentBase or McuComponentBase) should return the
 * ExecutiveCore constants from their componentId() / componentName() /
 * componentType() / label() overrides:
 *
 * @code
 * class MyExecutive : public McuComponentBase, public ExecutiveCore {
 *   uint16_t componentId() const noexcept override {
 *     return ExecutiveCore::COMPONENT_ID;
 *   }
 *   const char* componentName() const noexcept override {
 *     return ExecutiveCore::COMPONENT_NAME;
 *   }
 *   // ... etc
 * };
 * @endcode
 */
class ExecutiveCore : public IExecutive {
public:
  /* ----------------------------- Identity Constants ----------------------------- */

  /// Component ID for executives (always 0 -- root component).
  static constexpr uint16_t COMPONENT_ID = 0;

  /// Component name for collision detection.
  static constexpr const char* COMPONENT_NAME = "Executive";

  /// Default diagnostic label.
  static constexpr const char* COMPONENT_LABEL = "EXECUTIVE";

  /// Component type classification.
  static constexpr system_core::system_component::ComponentType COMPONENT_TYPE =
      system_core::system_component::ComponentType::EXECUTIVE;

  /* ----------------------------- Lifecycle ----------------------------- */

  ~ExecutiveCore() override = default;

  /**
   * @brief Main control / scheduler loop entry.
   * @note NOT RT-safe during startup / shutdown phases.
   * @note Inner loop may be RT-safe depending on implementation.
   */
  [[nodiscard]] RunResult run() noexcept override = 0;

  /**
   * @brief Request graceful shutdown.
   * @note Thread-safe / ISR-safe depending on implementation; sets a flag.
   */
  void shutdown() noexcept override = 0;

  /**
   * @brief Check if shutdown has been requested.
   * @note RT-safe: O(1) flag read in all expected implementations.
   */
  [[nodiscard]] bool isShutdownRequested() const noexcept override = 0;

  /**
   * @brief Number of completed execution cycles since run() started.
   * @note RT-safe: O(1) counter read in all expected implementations.
   */
  [[nodiscard]] uint64_t cycleCount() const noexcept override = 0;

protected:
  ExecutiveCore() noexcept = default;

  // Non-copyable (executives have identity).
  ExecutiveCore(const ExecutiveCore&) = delete;
  ExecutiveCore& operator=(const ExecutiveCore&) = delete;

  // Non-movable (executives are typically owned by an app and not relocated).
  ExecutiveCore(ExecutiveCore&&) = delete;
  ExecutiveCore& operator=(ExecutiveCore&&) = delete;
};

} // namespace executive

#endif // APEX_EXECUTIVE_CORE_EXECUTIVE_CORE_HPP
