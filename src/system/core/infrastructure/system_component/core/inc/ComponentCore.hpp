#ifndef APEX_SYSTEM_COMPONENT_CORE_COMPONENT_CORE_HPP
#define APEX_SYSTEM_COMPONENT_CORE_COMPONENT_CORE_HPP
/**
 * @file ComponentCore.hpp
 * @brief Shared concrete base for IComponent implementations.
 *
 * Owns the identity, lifecycle, and registration state common to every
 * IComponent implementation. Has no platform dependencies (no
 * filesystem, heap, or threads in the base) so it is safe for both
 * bare-metal MCU and POSIX targets.
 *
 * Tier (from project tiering policy):
 *   - Has concrete state (status, instance index, init flag, ...)
 *   - Not instantiable: componentId(), componentName(), componentType(),
 *     label(), and doInit() remain pure virtual.
 *
 * Derivatives:
 *   - SystemComponentBase (posix/) - adds TPRM, internal bus, command
 *     handling, data descriptors, configured/locked semantics, logging.
 *   - McuComponentBase (mcu/) - adds MCU-specific constraints
 *     (deletes moves) but otherwise uses ComponentCore as-is.
 *
 * Initialization template:
 *   init() runs preInitCheck() -> preInit() -> doInit(). Derived classes
 *   override the hooks rather than init() itself, so the lifecycle stays
 *   consistent across the hierarchy.
 *
 * RT Constraints:
 *   - All query methods are RT-safe (const, noexcept, O(1)).
 *   - init() and reset() are boot-time only.
 */

#include "src/system/core/infrastructure/system_component/base/inc/ComponentType.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/IComponent.hpp"

#include <stdint.h>

namespace system_core {
namespace system_component {

/* ----------------------------- Constants ----------------------------- */

/// Invalid full UID (returned before registration).
constexpr uint32_t INVALID_COMPONENT_UID = 0xFFFFFFFFu;

/* ----------------------------- ComponentCore ----------------------------- */

/**
 * @class ComponentCore
 * @brief Shared concrete IComponent base with identity and lifecycle state.
 *
 * Derived classes must implement:
 *   - componentId(), componentName(), componentType(), label()
 *   - doInit()
 *
 * Optionally override:
 *   - preInitCheck() - return non-zero to fail init() before doInit().
 *   - preInit()      - run setup just before doInit().
 *   - doReset()      - cleanup logic for reset().
 */
class ComponentCore : public IComponent {
public:
  ComponentCore() noexcept = default;
  ~ComponentCore() override = default;

  // Non-copyable (components have identity).
  ComponentCore(const ComponentCore&) = delete;
  ComponentCore& operator=(const ComponentCore&) = delete;

  // Movable (matches IComponent contract).
  ComponentCore(ComponentCore&&) noexcept = default;
  ComponentCore& operator=(ComponentCore&&) noexcept = default;

  /* ----------------------------- Lifecycle ----------------------------- */

  /**
   * @brief Boot-time initialization (template method).
   * @return Status code (0 on success).
   * @note Idempotent: returns current status if already initialized.
   * @note NOT RT-safe.
   */
  [[nodiscard]] uint8_t init() noexcept override {
    if (initialized_) {
      return status_;
    }
    const uint8_t PRE_RESULT = preInitCheck();
    if (PRE_RESULT != 0) {
      status_ = PRE_RESULT;
      return PRE_RESULT;
    }
    preInit();
    const uint8_t RESULT = doInit();
    status_ = RESULT;
    if (RESULT == 0) {
      initialized_ = true;
      lastError_ = nullptr;
    }
    return RESULT;
  }

  /**
   * @brief Reset component state.
   * @note Calls doReset() hook, then clears initialized flag.
   * @note NOT RT-safe.
   */
  void reset() noexcept override {
    doReset();
    initialized_ = false;
    status_ = 0;
    lastError_ = nullptr;
  }

  /* ----------------------------- Status ----------------------------- */

  [[nodiscard]] uint8_t status() const noexcept override { return status_; }
  [[nodiscard]] bool isInitialized() const noexcept override { return initialized_; }

  /* ----------------------------- Registration ----------------------------- */

  [[nodiscard]] uint32_t fullUid() const noexcept override { return fullUid_; }
  [[nodiscard]] uint8_t instanceIndex() const noexcept override { return instanceIndex_; }
  [[nodiscard]] bool isRegistered() const noexcept override { return registered_; }

  /**
   * @brief Set instance index and compute full UID.
   * @param instanceIdx Instance index assigned by the executive.
   * @note Full UID = (componentId << 8) | instanceIdx.
   * @note RT-safe (no allocation).
   */
  void setInstanceIndex(uint8_t instanceIdx) noexcept {
    instanceIndex_ = instanceIdx;
    fullUid_ = (static_cast<uint32_t>(componentId()) << 8) | instanceIdx;
    registered_ = true;
  }

  /* ----------------------------- Diagnostics ----------------------------- */

  /**
   * @brief Get last error context (string literal or nullptr).
   * @note RT-safe: simple pointer read.
   */
  [[nodiscard]] const char* lastError() const noexcept { return lastError_; }

protected:
  /* ----------------------------- Hooks ----------------------------- */

  /**
   * @brief Initialization hook (pure virtual).
   * @return 0 on success, non-zero error code on failure.
   * @note Called by init() after preInitCheck() and preInit().
   */
  [[nodiscard]] virtual uint8_t doInit() noexcept = 0;

  /**
   * @brief Pre-condition check before init() runs doInit().
   * @return 0 to proceed, non-zero to abort with that status.
   * @note Default returns 0 (no precondition).
   * @note Override to gate init() on configuration / external state.
   */
  [[nodiscard]] virtual uint8_t preInitCheck() noexcept { return 0; }

  /**
   * @brief Pre-init hook called after preInitCheck() succeeds.
   * @note Default does nothing.
   * @note Override to apply staged params, prep state, etc.
   */
  virtual void preInit() noexcept {}

  /**
   * @brief Reset hook (default no-op).
   * @note Override to perform cleanup before initialized flag clears.
   */
  virtual void doReset() noexcept {}

  /* ----------------------------- Mutators ----------------------------- */

  void setStatus(uint8_t s) noexcept { status_ = s; }
  void setLastError(const char* err) noexcept { lastError_ = err; }

private:
  uint32_t fullUid_{INVALID_COMPONENT_UID};
  const char* lastError_{nullptr};
  uint8_t instanceIndex_{0};
  uint8_t status_{0};
  bool initialized_{false};
  bool registered_{false};
};

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_COMPONENT_CORE_COMPONENT_CORE_HPP
