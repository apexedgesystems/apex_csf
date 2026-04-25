#ifndef APEX_FILESYSTEM_MCU_NULL_FILE_SYSTEM_HPP
#define APEX_FILESYSTEM_MCU_NULL_FILE_SYSTEM_HPP
/**
 * @file NullFileSystem.hpp
 * @brief No-op filesystem stub for resource-constrained systems.
 *
 * Design:
 *   - All operations are no-ops that return success
 *   - No heap allocation
 *   - No dependencies on std::filesystem
 *   - Suitable for bare-metal MCU targets without storage
 *
 * Use Cases:
 *   - McuExecutive on MCUs without filesystem
 *   - Testing components that optionally use filesystem
 *   - Environments where logging/storage is disabled
 *
 * Trade-offs vs ApexFileSystem:
 *   - No actual file operations (all writes are discarded)
 *   - No directory creation
 *   - No archive operations
 *   - No TPRM extraction from files
 *
 * For MCUs with flash storage, a flash-backed filesystem can be layered on top.
 *
 * @note RT-safe: All methods are O(1) with no allocation.
 */

#include "src/system/core/components/filesystem/base/inc/IFileSystem.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/ComponentType.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/IComponent.hpp"

#include <cstdint>

namespace system_core {
namespace filesystem {
namespace mcu {

/* ----------------------------- Constants ----------------------------- */

/// FileSystem component ID (matches apex filesystem).
constexpr std::uint16_t NULL_FILESYSTEM_COMPONENT_ID = 2;

/* ----------------------------- NullFileSystem ----------------------------- */

/**
 * @class NullFileSystem
 * @brief No-op IComponent implementation for environments without storage.
 *
 * Provides a filesystem interface where all operations succeed but do nothing.
 * Useful for McuExecutive deployments where no persistent storage is available
 * or needed.
 *
 * Usage:
 * @code
 * NullFileSystem fs;
 * fs.init();
 * // All file operations are no-ops
 * @endcode
 *
 * @note RT-safe: All methods are O(1) with no allocation.
 */
class NullFileSystem : public system_component::IComponent, public IFileSystem {
public:
  /** @brief Default constructor. */
  NullFileSystem() noexcept = default;

  ~NullFileSystem() override = default;

  // Non-copyable, non-movable
  NullFileSystem(const NullFileSystem&) = delete;
  NullFileSystem& operator=(const NullFileSystem&) = delete;
  NullFileSystem(NullFileSystem&&) = delete;
  NullFileSystem& operator=(NullFileSystem&&) = delete;

  /* ----------------------------- IComponent: Identity ----------------------------- */

  /**
   * @brief Get component type identifier.
   * @return FileSystem component ID (2).
   * @note RT-safe: O(1).
   */
  [[nodiscard]] std::uint16_t componentId() const noexcept override {
    return NULL_FILESYSTEM_COMPONENT_ID;
  }

  /**
   * @brief Get component name.
   * @return "NullFileSystem".
   * @note RT-safe: O(1).
   */
  [[nodiscard]] const char* componentName() const noexcept override { return "NullFileSystem"; }

  /**
   * @brief Get component type classification.
   * @return ComponentType::CORE.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] system_component::ComponentType componentType() const noexcept override {
    return system_component::ComponentType::CORE;
  }

  /**
   * @brief Get diagnostic label.
   * @return "NULL_FS".
   * @note RT-safe: O(1).
   */
  [[nodiscard]] const char* label() const noexcept override { return "NULL_FS"; }

  /* ----------------------------- IComponent: Lifecycle ----------------------------- */

  /**
   * @brief Initialize filesystem (no-op).
   * @return 0 (SUCCESS).
   * @note RT-safe: O(1).
   */
  [[nodiscard]] std::uint8_t init() noexcept override {
    initialized_ = true;
    return 0;
  }

  /**
   * @brief Reset filesystem state (no-op).
   * @note RT-safe: O(1).
   */
  void reset() noexcept override { initialized_ = false; }

  /**
   * @brief Get last operation status.
   * @return 0 (SUCCESS).
   * @note RT-safe: O(1).
   */
  [[nodiscard]] std::uint8_t status() const noexcept override { return 0; }

  /**
   * @brief Check if initialized.
   * @return true after init() called.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] bool isInitialized() const noexcept override { return initialized_; }

  /* ----------------------------- IComponent: Registration ----------------------------- */

  /**
   * @brief Get full component UID.
   * @return Full UID = (componentId << 8) | instanceIndex.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] std::uint32_t fullUid() const noexcept override { return fullUid_; }

  /**
   * @brief Get instance index.
   * @return Instance index assigned during registration.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] std::uint8_t instanceIndex() const noexcept override { return instanceIndex_; }

  /**
   * @brief Check if registered with executive.
   * @return true after setInstanceIndex() called.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] bool isRegistered() const noexcept override { return registered_; }

  /**
   * @brief Set instance index (called by executive).
   * @param idx Instance index.
   */
  void setInstanceIndex(std::uint8_t idx) noexcept {
    instanceIndex_ = idx;
    fullUid_ = (static_cast<std::uint32_t>(componentId()) << 8) | idx;
    registered_ = true;
  }

  /* ----------------------------- Filesystem Operations (No-ops) ----------------------------- */

  /**
   * @brief Check if a path exists (always false).
   * @return false.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] bool exists(const char* /*path*/) const noexcept { return false; }

  /**
   * @brief Create directory (no-op, always succeeds).
   * @return true.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] bool createDirectory(const char* /*path*/) noexcept { return true; }

  /**
   * @brief Write data to file (no-op, always succeeds).
   * @return true.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] bool writeFile(const char* /*path*/, const void* /*data*/,
                               std::size_t /*size*/) noexcept {
    return true;
  }

  /**
   * @brief Read data from file (no-op, returns 0 bytes).
   * @return 0 (no bytes read).
   * @note RT-safe: O(1).
   */
  [[nodiscard]] std::size_t readFile(const char* /*path*/, void* /*buffer*/,
                                     std::size_t /*maxSize*/) noexcept {
    return 0;
  }

  /**
   * @brief Delete file (no-op, always succeeds).
   * @return true.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] bool deleteFile(const char* /*path*/) noexcept { return true; }

private:
  std::uint32_t fullUid_{0xFFFFFFFF};
  std::uint8_t instanceIndex_{0};
  bool initialized_{false};
  bool registered_{false};
};

} // namespace mcu
} // namespace filesystem
} // namespace system_core

#endif // APEX_FILESYSTEM_MCU_NULL_FILE_SYSTEM_HPP
