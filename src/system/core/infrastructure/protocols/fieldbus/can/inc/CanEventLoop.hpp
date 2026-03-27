#ifndef APEX_PROTOCOLS_FIELDBUS_CAN_CAN_EVENT_LOOP_HPP
#define APEX_PROTOCOLS_FIELDBUS_CAN_CAN_EVENT_LOOP_HPP
/**
 * @file CanEventLoop.hpp
 * @brief Event-driven CAN frame reception using epoll and RT-safe callbacks.
 *
 * Provides a unified event loop that monitors multiple CAN devices and
 * dispatches frame events via RT-safe Delegate callbacks.
 *
 * Design goals:
 *  - Zero-allocation on hot paths (callbacks via Delegate)
 *  - Support multiple CAN interfaces in one event loop
 *  - Bounded execution (maxEvents limit per poll)
 *
 * RT-safety:
 *  - add/remove: NOT RT-safe (modifies epoll set)
 *  - poll(): RT-safe after setup (epoll_wait + callback dispatch)
 *  - Callbacks are invoked synchronously in poll()
 */

#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/CanBusAdapter.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <cstddef>
#include <cstdint>
#include <sys/epoll.h>
#include <unistd.h>
#include <vector>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace can {

/* ----------------------------- CanEventCallback ---------------------------- */

/**
 * @brief Callback signature for CAN frame events.
 *
 * Invoked when frames are available on a CAN device.
 *
 * @param ctx      User context pointer.
 * @param adapter  The CANBusAdapter that has data available.
 * @param events   Epoll events (EPOLLIN, EPOLLERR, etc.).
 */
using CanEventCallback = concurrency::Delegate<void, CANBusAdapter*, std::uint32_t>;

/* ----------------------------- CanEventLoop -------------------------------- */

/**
 * @class CanEventLoop
 * @brief Epoll-based event loop for multiple CAN devices.
 *
 * Monitors file descriptors of registered CAN devices and invokes
 * callbacks when frames are available. Supports up to maxDevices
 * simultaneous CAN interfaces.
 *
 * Usage:
 * @code
 *   CanEventLoop loop;
 *   loop.init();
 *
 *   CANBusAdapter can0("ECU0", "can0");
 *   can0.configure(CanConfig{});
 *
 *   auto handler = [](void* ctx, CanDevice* dev, uint32_t events) noexcept {
 *     CanFrame frame;
 *     while (dev->recv(frame, 0) == Status::SUCCESS) {
 *       // Process frame
 *     }
 *   };
 *   loop.add(&can0, CanEventCallback{handler, nullptr});
 *
 *   // Event loop
 *   while (running) {
 *     loop.poll(100);  // 100ms timeout
 *   }
 * @endcode
 */
class CanEventLoop {
public:
  /** @brief Maximum devices that can be monitored. */
  static constexpr std::size_t MAX_DEVICES = 16;

  /** @brief Maximum events to process per poll() call. */
  static constexpr int MAX_EVENTS = 16;

  /**
   * @brief Construct an uninitialized event loop.
   * @note Call init() before use.
   */
  CanEventLoop() = default;

  /** @brief Destructor closes epoll fd. */
  ~CanEventLoop();

  // Non-copyable, non-movable (owns epoll fd)
  CanEventLoop(const CanEventLoop&) = delete;
  CanEventLoop& operator=(const CanEventLoop&) = delete;
  CanEventLoop(CanEventLoop&&) = delete;
  CanEventLoop& operator=(CanEventLoop&&) = delete;

  /**
   * @brief Initialize the epoll instance.
   * @return true on success, false on failure.
   * @note NOT RT-safe: Creates epoll fd.
   */
  bool init();

  /**
   * @brief Check if initialized.
   * @return true if epoll fd is valid.
   */
  [[nodiscard]] bool isInitialized() const noexcept { return epfd_ >= 0; }

  /**
   * @brief Add a CAN adapter to the event loop.
   *
   * @param adapter  CAN adapter to monitor (must be configured).
   * @param callback Callback invoked when adapter has data.
   * @return true on success, false if full or epoll_ctl failed.
   * @note NOT RT-safe: Modifies epoll set.
   * @note The adapter must remain valid while registered.
   */
  bool add(CANBusAdapter* adapter, CanEventCallback callback);

  /**
   * @brief Remove a CAN adapter from the event loop.
   *
   * @param adapter Adapter to remove.
   * @return true on success, false if not found.
   * @note NOT RT-safe: Modifies epoll set.
   */
  bool remove(CANBusAdapter* adapter);

  /**
   * @brief Poll for events and dispatch callbacks.
   *
   * Waits for up to timeoutMs milliseconds for events on any registered
   * device. When events occur, invokes the associated callback.
   *
   * @param timeoutMs Timeout in milliseconds:
   *                  - < 0: block indefinitely
   *                  - == 0: non-blocking poll
   *                  - > 0: wait up to timeoutMs
   * @return Number of callbacks dispatched (0 to MAX_EVENTS).
   * @note RT-safe after setup (bounded execution, no allocation).
   */
  int poll(int timeoutMs);

  /**
   * @brief Get number of registered adapters.
   * @return Count of adapters in the event loop.
   */
  [[nodiscard]] std::size_t adapterCount() const noexcept { return adapters_.size(); }

  /**
   * @brief Accessor for underlying epoll fd (for advanced use cases).
   * @return epoll file descriptor, or -1 if not initialized.
   */
  [[nodiscard]] int epollFd() const noexcept { return epfd_; }

private:
  /** @brief Entry tracking adapter and callback. */
  struct AdapterEntry {
    CANBusAdapter* adapter{nullptr};
    CanEventCallback callback;
    int fd{-1}; // Cached fd for epoll
  };

  int epfd_{-1};                       ///< Epoll file descriptor.
  std::vector<AdapterEntry> adapters_; ///< Registered adapters.

  /**
   * @brief Find entry by adapter pointer.
   * @param adapter Adapter to find.
   * @return Iterator to entry, or adapters_.end() if not found.
   */
  auto findAdapter(CANBusAdapter* adapter) -> decltype(adapters_.begin());
};

} // namespace can
} // namespace fieldbus
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_FIELDBUS_CAN_CAN_EVENT_LOOP_HPP
