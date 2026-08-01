#ifndef APEX_SYSTEM_LOGS_LOG_DRAIN_SERVICE_HPP
#define APEX_SYSTEM_LOGS_LOG_DRAIN_SERVICE_HPP
/**
 * @file LogDrainService.hpp
 * @brief Shared drain thread for every async log backend in the process.
 *
 * One I/O thread services all registered AsyncLogBackend rings instead of
 * one thread per backend. Producers keep their lock-free enqueue and wake
 * the shared thread through one eventfd; the drain thread sweeps the
 * registered rings round-robin and parks on the eventfd when every ring is
 * empty. Each backend keeps its own file descriptor, ring, and counters --
 * the log-file-per-component layout is unchanged; only the thread that
 * empties the rings is shared.
 *
 * Registration and unregistration are control-plane (mutex-guarded, called
 * from backend start/stop). Unregister blocks until the drain thread
 * cannot be touching the departing backend, so backend teardown is safe by
 * construction.
 *
 * RT Constraints:
 *  - notify() is RT-safe: one non-blocking eventfd write, no locks.
 *  - registerBackend()/unregisterBackend() are NOT RT-safe.
 *  - The service thread starts on first registration and parks when idle.
 *
 * Linux-only (eventfd); non-Linux builds keep per-backend threads.
 */

#if defined(__linux__)

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>

namespace logs {

class AsyncLogBackend;

/**
 * @class LogDrainService
 * @brief Process-wide drain thread shared by async log backends.
 *
 * Access through instance(); backends self-register. The registry is a
 * fixed-size slot array: the drain thread reads slots with acquire loads
 * and never takes the registry mutex on its sweep, so registration churn
 * cannot stall draining.
 */
class LogDrainService {
public:
  /// Maximum simultaneously registered backends.
  static constexpr std::size_t MAX_BACKENDS = 64;

  /** @brief The process-wide service. First use constructs it. */
  static LogDrainService& instance() noexcept;

  /**
   * @brief Register a backend for draining.
   * @return true on success; false if the registry is full or the drain
   *         thread could not be started.
   * @note NOT RT-safe: mutex + possible thread start.
   */
  bool registerBackend(AsyncLogBackend* backend) noexcept;

  /**
   * @brief Unregister a backend, blocking until the drain thread can no
   *        longer touch it.
   * @note NOT RT-safe: mutex + wait for sweep hand-off.
   */
  void unregisterBackend(AsyncLogBackend* backend) noexcept;

  /**
   * @brief Wake the drain thread (queue transitioned empty -> non-empty).
   * @note RT-safe: one non-blocking eventfd write; no locks.
   */
  void notify() noexcept;

private:
  LogDrainService() noexcept;
  ~LogDrainService() noexcept;

  LogDrainService(const LogDrainService&) = delete;
  LogDrainService& operator=(const LogDrainService&) = delete;

  void drainLoop() noexcept;

  std::atomic<AsyncLogBackend*> slots_[MAX_BACKENDS]{}; ///< Sweep targets.
  std::atomic<std::size_t> highWater_{0};               ///< Slots ever used (sweep bound).

  std::mutex registryMutex_;                 ///< Guards register/unregister.
  std::condition_variable idleCv_;           ///< Unregister waits for sweep hand-off.
  std::atomic<std::uint64_t> sweepEpoch_{0}; ///< Bumped at each sweep top.

  int wakeFd_{-1};                   ///< Shared eventfd producers signal.
  std::thread drainThread_;          ///< The one I/O thread.
  std::atomic<bool> running_{false}; ///< Drain thread control.
};

} // namespace logs

#endif // defined(__linux__)

#endif // APEX_SYSTEM_LOGS_LOG_DRAIN_SERVICE_HPP
