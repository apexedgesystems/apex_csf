#ifndef APEX_PROTOCOLS_WIRELESS_BLUETOOTH_RFCOMM_ADAPTER_HPP
#define APEX_PROTOCOLS_WIRELESS_BLUETOOTH_RFCOMM_ADAPTER_HPP
/**
 * @file RfcommAdapter.hpp
 * @brief Linux implementation of RFCOMM Bluetooth connections.
 *
 * Provides a stream socket interface to Bluetooth RFCOMM connections.
 * Supports two modes of operation:
 *
 * 1. Production Mode:
 *    - Default constructor
 *    - configure() creates AF_BLUETOOTH socket
 *    - connect() establishes connection to remote device
 *
 * 2. Test Mode (FD Injection):
 *    - Constructor with pre-connected FD
 *    - configure()/connect() are no-ops (already connected)
 *    - Used with RfcommLoopback for unit testing
 *
 * This design keeps test concerns out of production code path.
 */

#include "src/system/core/infrastructure/protocols/common/inc/ByteTrace.hpp"
#include "RfcommDevice.hpp"

namespace apex {
namespace protocols {
namespace wireless {
namespace bluetooth {

/* ----------------------------- RfcommAdapter ----------------------------- */

/**
 * @class RfcommAdapter
 * @brief Linux RFCOMM socket implementation.
 *
 * Implements RfcommDevice interface using AF_BLUETOOTH/BTPROTO_RFCOMM sockets.
 * Supports FD injection for testing without Bluetooth hardware.
 *
 * Usage (Production):
 * @code
 * RfcommAdapter adapter;
 * RfcommConfig cfg;
 * cfg.remoteAddress = BluetoothAddress::fromString("AA:BB:CC:DD:EE:FF");
 * cfg.channel = 1;
 * adapter.configure(cfg);
 * adapter.connect();
 * // ... use adapter ...
 * adapter.disconnect();
 * @endcode
 *
 * Usage (Test):
 * @code
 * RfcommLoopback loopback;
 * loopback.open();
 * RfcommAdapter adapter(loopback.releaseClientFd());
 * // No configure()/connect() needed - already connected via loopback
 * adapter.write(...);
 * adapter.read(...);
 * @endcode
 */
class RfcommAdapter : public RfcommDevice, public apex::protocols::ByteTrace {
public:
  /**
   * @brief Default constructor for production use.
   * @note NOT RT-safe: May allocate.
   *
   * Use configure() + connect() to establish connection.
   */
  RfcommAdapter() noexcept;

  /**
   * @brief FD injection constructor for testing.
   * @param connectedFd Pre-connected socket FD (e.g., from RfcommLoopback).
   * @note NOT RT-safe: May allocate.
   *
   * Adapter takes ownership of FD and will close it on destruction.
   * configure() and connect() become no-ops when using this constructor.
   */
  explicit RfcommAdapter(int connectedFd) noexcept;

  /**
   * @brief Destructor closes connection.
   * @note NOT RT-safe: Socket close syscall.
   */
  ~RfcommAdapter() override;

  // Non-copyable
  RfcommAdapter(const RfcommAdapter&) = delete;
  RfcommAdapter& operator=(const RfcommAdapter&) = delete;

  // Non-movable (due to ByteTrace atomic)
  RfcommAdapter(RfcommAdapter&&) = delete;
  RfcommAdapter& operator=(RfcommAdapter&&) = delete;

  /* ----------------------------- RfcommDevice Interface ----------------------------- */

  [[nodiscard]] const char* description() const noexcept override;
  [[nodiscard]] Status configure(const RfcommConfig& cfg) override;
  [[nodiscard]] Status connect() override;
  [[nodiscard]] Status disconnect() noexcept override;
  [[nodiscard]] bool isConnected() const noexcept override;

  [[nodiscard]] Status read(apex::compat::mutable_bytes_span buffer, std::size_t& bytesRead,
                            int timeoutMs) noexcept override;
  [[nodiscard]] Status write(apex::compat::bytes_span data, std::size_t& bytesWritten,
                             int timeoutMs) noexcept override;

  [[nodiscard]] const RfcommStats& stats() const noexcept override;
  void resetStats() noexcept override;
  [[nodiscard]] int fd() const noexcept override;

  /* ----------------------------- Additional API ----------------------------- */

  /**
   * @brief Check if adapter was created via FD injection.
   * @return true if constructed with pre-connected FD.
   * @note RT-safe: O(1), no allocation.
   */
  [[nodiscard]] bool isInjectedFd() const noexcept { return injectedFd_; }

  /**
   * @brief Get the current configuration.
   * @return Reference to configuration struct.
   * @note RT-safe: O(1), no allocation.
   */
  [[nodiscard]] const RfcommConfig& config() const noexcept { return config_; }

private:
  int fd_{-1};
  bool injectedFd_{false};
  bool configured_{false};
  bool connected_{false};
  RfcommConfig config_{};
  RfcommStats stats_{};
  char description_[64]{};

  /**
   * @brief Update description string based on current state.
   */
  void updateDescription() noexcept;
};

} // namespace bluetooth
} // namespace wireless
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_WIRELESS_BLUETOOTH_RFCOMM_ADAPTER_HPP
