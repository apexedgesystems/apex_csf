#ifndef APEX_PROTOCOLS_FIELDBUS_CAN_VCAN_INTERFACE_HPP
#define APEX_PROTOCOLS_FIELDBUS_CAN_VCAN_INTERFACE_HPP

/**
 * @file VCanInterface.hpp
 * @brief Minimal helper to create/teardown a Linux virtual CAN (vcan) interface for tests.
 *
 * Purpose
 *  - Keep CAN tests self-contained and idempotent.
 *  - Avoid coupling to project logging; emit minimal stderr on failure paths.
 *  - Linux-only; uses `modprobe` and `ip link` via std::system().
 *
 * Caveats
 *  - Requires NET_ADMIN privileges; set useSudo=true if not root.
 *  - Concurrent tests should use unique interface names to avoid collisions.
 *  - Not intended for production; this is a test utility.
 */

#include <string>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace can {
namespace util {

/* ---------------------------- VCanInterface ----------------------------- */

/**
 * @class VCanInterface
 * @brief Wrapper around `modprobe vcan` and `ip link` commands (best-effort).
 *
 * Semantics
 *  - @ref setup() is idempotent: creates the device if missing, ensures it's up.
 *  - @ref teardown() is idempotent: succeeds if device is already absent.
 *  - By default, destructor attempts teardown (best-effort).
 */
class VCanInterface {
public:
  /**
   * @brief Construct a helper for a named vcan device.
   * @param interfaceName Interface name (e.g., "vcan0").
   * @param autoTeardown  If true, attempt to delete the interface in the destructor.
   * @param useSudo       If true, prefix commands with "sudo " (useful in CI).
   * @note NOT RT-safe: allocates for string copy
   */
  VCanInterface(const std::string& interfaceName = "vcan0", bool autoTeardown = true,
                bool useSudo = true);

  /**
   * @brief Destructor: optionally deletes the interface (best-effort).
   * @note NOT RT-safe: executes system commands
   */
  ~VCanInterface();

  /**
   * @brief Create (if missing) and bring up the vcan interface.
   *
   * Steps (best-effort):
   *  1) `modprobe vcan`
   *  2) If not present: `ip link add dev <name> type vcan`
   *  3) `ip link set <name> up`
   * @return true on success; false otherwise.
   * @note NOT RT-safe: executes system commands
   */
  [[nodiscard]] bool setup();

  /**
   * @brief Delete the vcan interface (idempotent).
   * @return true on success; false otherwise.
   * @note NOT RT-safe: executes system commands
   */
  [[nodiscard]] bool teardown();

  /**
   * @return Interface name (e.g., "vcan0").
   * @note RT-safe: no allocation or I/O
   */
  const std::string& interfaceName() const noexcept { return ifName_; }

  /**
   * @return Whether sudo is used for commands.
   * @note RT-safe: no allocation or I/O
   */
  bool useSudo() const noexcept { return useSudo_; }

private:
  // Build-time helpers
  std::string cmdPrefix() const;          // "" or "sudo "
  bool isPresent() const;                 // true if `ip link show <name>` succeeds
  bool run(const std::string& cmd) const; // std::system wrapper (quiet by default)

private:
  std::string ifName_;
  bool autoTeardown_;
  bool useSudo_;
};

} // namespace util
} // namespace can
} // namespace fieldbus
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_FIELDBUS_CAN_VCAN_INTERFACE_HPP
