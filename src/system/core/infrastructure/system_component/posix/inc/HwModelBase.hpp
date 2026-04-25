#ifndef APEX_SYSTEM_COMPONENT_HW_MODEL_BASE_HPP
#define APEX_SYSTEM_COMPONENT_HW_MODEL_BASE_HPP
/**
 * @file HwModelBase.hpp
 * @brief Base class for hardware emulation models.
 *
 * Hardware models emulate real hardware behavior in software:
 *   - Virtual CAN bus (message routing, timing)
 *   - Virtual serial ports
 *   - Sensor emulators (IMU, GPS, magnetometer)
 *   - Actuator emulators (motor controllers, servos)
 *
 * HwModelBase inherits task machinery from SchedulableComponentBase and adds
 * ComponentType::HW_MODEL classification. Use this base class for models that
 * emulate hardware behavior rather than environmental phenomena.
 *
 * Virtual Transport Framework:
 *   HW_MODELs declare a TransportKind via transportKind(). The framework
 *   calls provisionTransport() during registration, which delegates to the
 *   transport-agnostic provisioner in TransportLink.hpp. The model gets a
 *   transport fd for transportRead()/transportWrite(); the peer endpoint
 *   (device path or fd) is exposed for driver wiring.
 *
 *   Adding a new transport type requires NO changes to HwModelBase or any
 *   executive. Only TransportLink.hpp needs a new provisioner case.
 *
 * Hardware Abstraction Pattern:
 *   For hardware that can be either real or emulated, define an interface
 *   (e.g., IImu) that both the driver (ImuDriver : DriverBase) and the
 *   emulation model (ImuModel : HwModelBase) implement. This enables
 *   swapping real hardware for simulation without changing consuming code.
 *
 * All functions are RT-safe unless noted otherwise.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/SchedulableComponentBase.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/TransportLink.hpp"

#include <cerrno>
#include <cstddef>
#include <unistd.h>

namespace system_core {
namespace system_component {

/* ----------------------------- HwModelBase ----------------------------- */

/**
 * @class HwModelBase
 * @brief Abstract base for hardware emulation models.
 *
 * Provides organizational clarity for models that emulate hardware behavior
 * rather than environmental phenomena.
 *
 * Derived classes override transportKind() to declare what transport link
 * the framework should create. The framework calls provisionTransport()
 * before doInit(), so the transport is ready when the model initializes.
 *
 * Example derived classes: VCanBusModel, VSerialModel, ImuEmulator.
 */
class HwModelBase : public SchedulableComponentBase {
public:
  /** @brief Default constructor. */
  HwModelBase() noexcept = default;

  /** @brief Virtual destructor. Closes transport link fds. */
  ~HwModelBase() override { link_.close(); }

  // Non-copyable, non-movable (inherited from SchedulableComponentBase)
  HwModelBase(const HwModelBase&) = delete;
  HwModelBase& operator=(const HwModelBase&) = delete;
  HwModelBase(HwModelBase&&) = delete;
  HwModelBase& operator=(HwModelBase&&) = delete;

  /** @brief Component type for hardware emulation models. */
  [[nodiscard]] ComponentType componentType() const noexcept override {
    return ComponentType::HW_MODEL;
  }

  /**
   * @brief Declare the transport this HW_MODEL requires.
   *
   * Override in derived classes to request a specific transport link.
   * The framework calls provisionTransport() during registration to
   * create the appropriate virtual link based on this declaration.
   *
   * @return Transport kind. Default is NONE (no transport needed).
   * @note RT-safe: O(1).
   */
  [[nodiscard]] virtual TransportKind transportKind() const noexcept { return TransportKind::NONE; }

  /**
   * @brief Provision the virtual transport link.
   *
   * Called by the framework during component registration, before doInit().
   * Delegates to the transport-agnostic provisioner (TransportLink.hpp)
   * which creates the appropriate OS-level link based on transportKind().
   *
   * After provisioning, the model can use transportRead()/transportWrite()
   * and the executive can read peerDevicePath() or peerFd() for driver wiring.
   *
   * @return true if transport is ready (or not needed), false on failure.
   * @note NOT RT-safe: Delegates to provisionTransport() in TransportLink.hpp.
   */
  [[nodiscard]] bool provisionTransport() noexcept {
    return system_core::system_component::provisionTransport(transportKind(), link_);
  }

  /**
   * @brief Get the peer device path for serial transports.
   *
   * After provisionTransport() succeeds for a serial TransportKind,
   * returns the PTY slave path (e.g., "/dev/pts/3"). The executive
   * passes this to the matching driver's setDevicePath().
   *
   * @return Slave path, or empty string if not provisioned or not serial.
   * @note RT-safe: Returns pointer to internal storage.
   */
  [[nodiscard]] const char* peerDevicePath() const noexcept { return link_.peerPath; }

  /**
   * @brief Get the peer file descriptor for socketpair transports.
   *
   * After provisionTransport() succeeds for a socketpair TransportKind,
   * returns the peer fd. The executive passes this to the matching driver.
   *
   * @return Peer fd, or -1 if not provisioned or not socketpair.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] int peerFd() const noexcept { return link_.peerFd; }

  /**
   * @brief Get the transport file descriptor (model side).
   * @return fd, or -1 if no transport configured.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] int transportFd() const noexcept { return link_.modelFd; }

  /**
   * @brief Check if a transport link is available.
   * @return true if transportFd is valid.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] bool hasTransport() const noexcept { return link_.isValid(); }

  /**
   * @brief Get the underlying transport link.
   * @return Const reference to the TransportLink.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] const TransportLink& transportLink() const noexcept { return link_; }

protected:
  /**
   * @brief Read bytes from the transport link.
   *
   * Non-blocking read from the model-side transport fd.
   * Returns 0 bytes if nothing is available (does not block).
   *
   * @param buffer Destination buffer.
   * @param bufferSize Maximum bytes to read.
   * @param bytesRead Output: bytes actually read.
   * @return 0 on success (including zero bytes read), -1 on error.
   * @note RT-safe: Non-blocking read syscall.
   */
  int transportRead(void* buffer, std::size_t bufferSize, std::size_t& bytesRead) noexcept {
    bytesRead = 0;
    if (link_.modelFd < 0) {
      return -1;
    }
    auto n = ::read(link_.modelFd, buffer, bufferSize);
    if (n < 0) {
      return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
    }
    bytesRead = static_cast<std::size_t>(n);
    return 0;
  }

  /**
   * @brief Write bytes to the transport link.
   *
   * Non-blocking write to the model-side transport fd.
   *
   * @param data Source data.
   * @param dataSize Number of bytes to write.
   * @param bytesWritten Output: bytes actually written.
   * @return 0 on success, -1 on error.
   * @note RT-safe: Non-blocking write syscall.
   */
  int transportWrite(const void* data, std::size_t dataSize, std::size_t& bytesWritten) noexcept {
    bytesWritten = 0;
    if (link_.modelFd < 0) {
      return -1;
    }
    auto n = ::write(link_.modelFd, data, dataSize);
    if (n < 0) {
      return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
    }
    bytesWritten = static_cast<std::size_t>(n);
    return 0;
  }

private:
  TransportLink link_; ///< Provisioned transport (model fd + peer endpoint).
};

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_COMPONENT_HW_MODEL_BASE_HPP
