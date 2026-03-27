#ifndef APEX_SYSTEM_COMPONENT_LITE_COMPONENT_BASE_HPP
#define APEX_SYSTEM_COMPONENT_LITE_COMPONENT_BASE_HPP
/**
 * @file LiteComponentBase.hpp
 * @brief Minimal IComponent implementation for resource-constrained systems.
 *
 * Design:
 *   - Implements IComponent interface with static allocation
 *   - No heap, no std::filesystem, no std::thread, no std::vector
 *   - Fixed-size error message buffer (string literals only)
 *   - Suitable for bare-metal MCU targets with LiteExecutive
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
#include "src/system/core/infrastructure/system_component/base/inc/IComponent.hpp"

#include <stdint.h>

namespace system_core {
namespace system_component {
namespace lite {

/* ----------------------------- LiteComponentBase ----------------------------- */

/**
 * @class LiteComponentBase
 * @brief Minimal IComponent implementation for MCU targets.
 *
 * Provides the essential lifecycle and identity interface without any
 * heavyweight dependencies. Derived classes must implement:
 *   - componentId() - Return 16-bit component type ID
 *   - componentName() - Return static component name string
 *   - componentType() - Return component classification
 *   - label() - Return diagnostic label string
 *   - doInit() - Perform initialization logic
 *
 * Optionally override:
 *   - doReset() - Cleanup logic (default does nothing)
 *
 * @note RT-safe queries: status(), isInitialized(), fullUid(), instanceIndex().
 * @note NOT RT-safe: init(), reset() (may configure hardware, etc.).
 */
class LiteComponentBase : public IComponent {
public:
  /** @brief Default constructor. */
  LiteComponentBase() noexcept = default;

  /** @brief Virtual destructor. */
  ~LiteComponentBase() override = default;

  // Non-copyable (components have identity)
  LiteComponentBase(const LiteComponentBase&) = delete;
  LiteComponentBase& operator=(const LiteComponentBase&) = delete;

  // Non-movable (embedded components are typically static)
  LiteComponentBase(LiteComponentBase&&) = delete;
  LiteComponentBase& operator=(LiteComponentBase&&) = delete;

  /* ----------------------------- IComponent: Identity ----------------------------- */

  // componentId(), componentName(), componentType(), label() are pure virtual
  // Must be implemented by derived classes

  /* ----------------------------- IComponent: Lifecycle ----------------------------- */

  /**
   * @brief Boot-time initialization.
   * @return 0 on success, non-zero error code on failure.
   * @note NOT RT-safe: May configure hardware or perform I/O.
   * @note Idempotent: returns 0 if already initialized.
   */
  [[nodiscard]] uint8_t init() noexcept override {
    if (initialized_) {
      return 0; // Idempotent
    }
    const uint8_t RESULT = doInit();
    status_ = RESULT;
    if (RESULT == 0) {
      initialized_ = true;
      lastError_ = nullptr;
    }
    return RESULT;
  }

  /**
   * @brief Reset component state for re-initialization.
   * @note NOT RT-safe: May perform cleanup.
   * @note Calls doReset() hook, then clears initialized flag.
   */
  void reset() noexcept override {
    doReset();
    initialized_ = false;
    status_ = 0;
    lastError_ = nullptr;
  }

  /* ----------------------------- IComponent: Status ----------------------------- */

  /**
   * @brief Get last operation status code.
   * @return Status code (0 = SUCCESS).
   * @note RT-safe: O(1).
   */
  [[nodiscard]] uint8_t status() const noexcept override { return status_; }

  /**
   * @brief Check if component is initialized.
   * @return true after successful init().
   * @note RT-safe: O(1).
   */
  [[nodiscard]] bool isInitialized() const noexcept override { return initialized_; }

  /* ----------------------------- IComponent: Registration ----------------------------- */

  /**
   * @brief Get full component UID.
   * @return Full UID = (componentId << 8) | instanceIndex.
   * @note Valid after setInstanceIndex() called by executive.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] uint32_t fullUid() const noexcept override { return fullUid_; }

  /**
   * @brief Get instance index.
   * @return Instance index assigned during registration.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] uint8_t instanceIndex() const noexcept override { return instanceIndex_; }

  /**
   * @brief Check if component is registered with executive.
   * @return true after setInstanceIndex() called.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] bool isRegistered() const noexcept override { return registered_; }

  /* ----------------------------- Registration (Executive Use) ----------------------------- */

  /**
   * @brief Set instance index and compute full UID.
   * @param instanceIdx Instance index assigned by executive.
   * @note Called by LiteExecutive during registration.
   * @note RT-safe: O(1), no allocation.
   */
  void setInstanceIndex(uint8_t instanceIdx) noexcept {
    instanceIndex_ = instanceIdx;
    fullUid_ = (static_cast<uint32_t>(componentId()) << 8) | instanceIdx;
    registered_ = true;
  }

  /* ----------------------------- Diagnostics ----------------------------- */

  /**
   * @brief Get last error context (string literal or nullptr).
   * @return Error description, or nullptr if no error.
   * @note RT-safe: Simple pointer read.
   */
  [[nodiscard]] const char* lastError() const noexcept { return lastError_; }

protected:
  /* ----------------------------- Hooks (Derived) ----------------------------- */

  /**
   * @brief Initialization hook (pure virtual).
   * @return 0 on success, non-zero error code on failure.
   * @note Called by init() to perform actual initialization.
   * @note On failure, call setLastError() before returning.
   */
  [[nodiscard]] virtual uint8_t doInit() noexcept = 0;

  /**
   * @brief Reset hook (optional override).
   * @note Called by reset() before clearing initialized flag.
   * @note Override to perform cleanup.
   */
  virtual void doReset() noexcept {}

  /* ----------------------------- Mutators (Derived) ----------------------------- */

  /** @brief Update status code. */
  void setStatus(uint8_t s) noexcept { status_ = s; }

  /**
   * @brief Set error context (string literal).
   * @param err Error description (must be static string or nullptr).
   * @note RT-safe: Just stores pointer.
   */
  void setLastError(const char* err) noexcept { lastError_ = err; }

private:
  uint32_t fullUid_{0xFFFFFFFF};
  const char* lastError_{nullptr};
  uint8_t instanceIndex_{0};
  uint8_t status_{0};
  bool initialized_{false};
  bool registered_{false};
};

} // namespace lite
} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_COMPONENT_LITE_COMPONENT_BASE_HPP
