#ifndef APEX_FILESYSTEM_BASE_IFILESYSTEM_HPP
#define APEX_FILESYSTEM_BASE_IFILESYSTEM_HPP
/**
 * @file IFileSystem.hpp
 * @brief Tier-agnostic filesystem interface marker.
 *
 * Tier (from project tiering policy):
 *   - Pure virtual contract, no state, no platform deps.
 *   - Not instantiable; serves as the canonical filesystem identity for
 *     code that wants to refer to "any filesystem" regardless of tier.
 *
 * Current scope:
 *   - Marker interface. Both ApexFileSystem (POSIX) and NullFileSystem
 *     (MCU) inherit from it so they can be referred to through a common
 *     IFileSystem* pointer.
 *
 * Future scope:
 *   - Concrete path-based operations (exists/mkdir/read/write/remove)
 *     using `const char*` paths so RTOS targets (FreeRTOS+FATFS,
 *     ESP32 SPIFFS, Zephyr FS) can implement IFileSystem with their
 *     native APIs without pulling in `std::filesystem`.
 *   - The shape of those operations is intentionally deferred until a
 *     real RTOS target demands them; defining them prematurely would
 *     bias the API toward the POSIX implementation that already exists.
 *
 * Inheritance notes:
 *   - This interface deliberately does NOT inherit IComponent. POSIX
 *     filesystems already get their IComponent identity through
 *     SystemComponentBase, and MCU implementations through their own
 *     base. Inheriting IComponent here would form a diamond.
 */

namespace system_core {
namespace filesystem {

/* ----------------------------- IFileSystem ----------------------------- */

/**
 * @class IFileSystem
 * @brief Marker interface for filesystem implementations across tiers.
 *
 * Both POSIX (ApexFileSystem, FileSystemBase) and MCU (NullFileSystem)
 * inherit from this interface. Future iterations will add concrete
 * path-based operations once the API can be specified without bias
 * toward any single platform.
 */
class IFileSystem {
public:
  virtual ~IFileSystem() = default;

protected:
  IFileSystem() = default;
  IFileSystem(const IFileSystem&) = delete;
  IFileSystem& operator=(const IFileSystem&) = delete;
  IFileSystem(IFileSystem&&) = default;
  IFileSystem& operator=(IFileSystem&&) = default;
};

} // namespace filesystem
} // namespace system_core

#endif // APEX_FILESYSTEM_BASE_IFILESYSTEM_HPP
