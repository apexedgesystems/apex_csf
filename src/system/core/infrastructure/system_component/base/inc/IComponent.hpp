#ifndef APEX_SYSTEM_COMPONENT_BASE_ICOMPONENT_HPP
#define APEX_SYSTEM_COMPONENT_BASE_ICOMPONENT_HPP
/**
 * @file IComponent.hpp
 * @brief Minimal pure interface for system components.
 *
 * Design:
 *   - Zero dependencies beyond <stdint.h> and ComponentType.hpp
 *   - No std::filesystem, std::shared_ptr, std::vector, std::thread
 *   - Suitable for bare-metal MCU targets
 *   - Both SystemComponentBase (Apex) and McuComponentBase implement this
 *
 * RT Constraints:
 *   - All query methods are RT-safe (const, noexcept, O(1))
 *   - init() and reset() are boot-time only
 *
 * Implementations:
 *   - SystemComponentBase (apex/) - Full-featured, heap-using, POSIX filesystem
 *   - McuComponentBase (mcu/) - Minimal, static allocation, no filesystem
 */

#include "src/system/core/infrastructure/system_component/base/inc/ComponentType.hpp"

#include <stdint.h>

namespace system_core {
namespace system_component {

/* ----------------------------- IComponent ----------------------------- */

/**
 * @class IComponent
 * @brief Pure virtual interface for all system components.
 *
 * Defines the minimal contract that every component must satisfy.
 * This interface is deliberately lean - no heavy dependencies.
 *
 * Derived implementations:
 *   - SystemComponentBase: Full-featured for Linux/RTOS (ApexExecutive)
 *   - McuComponentBase: Minimal for bare-metal MCUs (McuExecutive)
 */
class IComponent {
public:
  /** @brief Virtual destructor. */
  virtual ~IComponent() = default;

  /**
   * @brief Get component type identifier.
   * @return 16-bit component type ID.
   * @note UID ranges: 0 = Executive, 1-100 = System, 101+ = Models.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] virtual uint16_t componentId() const noexcept = 0;

  /**
   * @brief Get component name for identification.
   * @return Null-terminated string (static lifetime).
   * @note RT-safe: O(1).
   */
  [[nodiscard]] virtual const char* componentName() const noexcept = 0;

  /**
   * @brief Get component type classification.
   * @return Component type (EXECUTIVE, CORE, SW_MODEL, etc.).
   * @note RT-safe: O(1).
   */
  [[nodiscard]] virtual ComponentType componentType() const noexcept = 0;

  /**
   * @brief Get diagnostic label for logging.
   * @return Static label string.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] virtual const char* label() const noexcept = 0;

  /**
   * @brief Boot-time initialization.
   * @return 0 on success, non-zero error code on failure.
   * @note NOT RT-safe: May allocate, perform I/O, or block.
   * @note Must be called before entering RT phase.
   */
  [[nodiscard]] virtual uint8_t init() noexcept = 0;

  /**
   * @brief Reset component state for re-initialization.
   * @note NOT RT-safe: May deallocate or perform cleanup.
   */
  virtual void reset() noexcept = 0;

  /**
   * @brief Get last operation status code.
   * @return Status code (0 = SUCCESS).
   * @note RT-safe: O(1).
   */
  [[nodiscard]] virtual uint8_t status() const noexcept = 0;

  /**
   * @brief Check if component is initialized.
   * @return true after successful init().
   * @note RT-safe: O(1).
   */
  [[nodiscard]] virtual bool isInitialized() const noexcept = 0;

  /**
   * @brief Get full component UID.
   * @return Full UID = (componentId << 8) | instanceIndex.
   * @note Valid after registration with executive.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] virtual uint32_t fullUid() const noexcept = 0;

  /**
   * @brief Get instance index (0 for first, 1 for second, etc.).
   * @return Instance index assigned during registration.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] virtual uint8_t instanceIndex() const noexcept = 0;

  /**
   * @brief Check if component is registered with executive.
   * @return true after setInstanceIndex() called.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] virtual bool isRegistered() const noexcept = 0;

protected:
  // Non-copyable (components have identity)
  IComponent() = default;
  IComponent(const IComponent&) = delete;
  IComponent& operator=(const IComponent&) = delete;
  IComponent(IComponent&&) = default;
  IComponent& operator=(IComponent&&) = default;
};

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_COMPONENT_BASE_ICOMPONENT_HPP
