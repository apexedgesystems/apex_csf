#ifndef APEX_UTILITIES_CONCURRENCY_THREAD_CONFIG_HPP
#define APEX_UTILITIES_CONCURRENCY_THREAD_CONFIG_HPP
/**
 * @file ThreadConfig.hpp
 * @brief Common thread configuration utilities for RT systems.
 *
 * Provides standalone functions for applying POSIX thread configuration
 * (affinity, scheduling policy/priority) that can be reused by:
 *   - ThreadPool workers
 *   - Executive primary threads
 *   - Any other threads requiring RT configuration
 *
 * @note NOT RT-safe: These functions make syscalls.
 */

#include <cstdint>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#include <sched.h>
#endif
#if defined(__linux__)
#include <sys/prctl.h>
#endif

namespace apex {
namespace concurrency {

/* ----------------------------- ThreadConfig ----------------------------- */

/**
 * @struct ThreadConfig
 * @brief Configuration for a single thread's RT settings.
 *
 * Used to configure thread affinity and scheduling policy at thread startup.
 * Applied once per thread to eliminate per-task syscall overhead.
 */
struct ThreadConfig {
  std::int8_t policy{0};                ///< POSIX policy (SCHED_OTHER=0, SCHED_FIFO=1, SCHED_RR=2).
  std::int8_t priority{0};              ///< POSIX priority (0 for SCHED_OTHER, 1-99 for RT).
  std::vector<std::uint8_t> affinity{}; ///< CPU affinity (empty = all CPUs).
};

/* ----------------------------- applyThreadConfig ----------------------------- */

/**
 * @brief Apply thread configuration to the calling thread.
 *
 * Applies CPU affinity and scheduling policy/priority using POSIX APIs.
 * Should be called at the start of a thread's execution.
 *
 * @param config Thread configuration to apply.
 * @param threadName Optional thread name for debugging (max 15 chars on Linux).
 * @return true if all settings applied successfully, false if any syscall failed.
 *
 * @note NOT RT-safe: Makes pthread syscalls.
 * @note Requires CAP_SYS_NICE for RT policies (SCHED_FIFO, SCHED_RR).
 */
inline bool applyThreadConfig(const ThreadConfig& config,
                              const char* threadName = nullptr) noexcept {
#if defined(__unix__) || defined(__APPLE__)
  bool success = true;

  // Set thread name for debugging (max 15 chars + null on Linux)
  if (threadName != nullptr) {
#if defined(__linux__)
    if (pthread_setname_np(pthread_self(), threadName) != 0) {
      success = false;
    }
#elif defined(__APPLE__)
    if (pthread_setname_np(threadName) != 0) {
      success = false;
    }
#endif
  }

  // Apply CPU affinity if specified
#if defined(__linux__)
  if (!config.affinity.empty()) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (auto core : config.affinity) {
      CPU_SET(core, &cpuset);
    }
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) != 0) {
      success = false;
    }
  }
#endif

  // Apply scheduling policy/priority if non-default
  if (config.policy != 0 || config.priority != 0) {
    sched_param sp{};
    sp.sched_priority = config.priority;
    if (pthread_setschedparam(pthread_self(), config.policy, &sp) != 0) {
      success = false;
    }
  }

  return success;
#else
  // Non-POSIX: no-op
  (void)config;
  (void)threadName;
  return true;
#endif
}

/* ----------------------------- setThreadTimerSlackNs ----------------------------- */

/**
 * @brief Set the calling thread's timer slack.
 *
 * The kernel may delay a timed wake by up to the thread's slack to
 * coalesce it with others: 50 us by default for ordinary policies, none
 * for real-time ones. A periodic thread that must wake on its deadline
 * under an ordinary policy sets the slack to one nanosecond, the
 * smallest value the kernel accepts.
 *
 * @param slackNs Slack in nanoseconds; 0 restores the thread's default.
 * @return The previous slack in nanoseconds, or -1 when the platform has
 *         no such control or refused the change.
 * @note NOT RT-safe: two syscalls. Linux only; elsewhere returns -1 and
 *       changes nothing.
 */
inline long setThreadTimerSlackNs(unsigned long slackNs) noexcept {
#if defined(__linux__)
  const int PREVIOUS = ::prctl(PR_GET_TIMERSLACK, 0, 0, 0, 0);
  if (PREVIOUS < 0 || ::prctl(PR_SET_TIMERSLACK, slackNs, 0, 0, 0) != 0) {
    return -1;
  }
  return PREVIOUS;
#else
  (void)slackNs;
  return -1;
#endif
}

/* ----------------------------- Convenience Overloads ----------------------------- */

/**
 * @brief Apply thread configuration from PoolConfig.
 *
 * Convenience overload that accepts PoolConfig (used by ThreadPool).
 *
 * @param config Pool configuration.
 * @param threadName Optional thread name.
 * @return true if successful.
 */
template <typename PoolConfigT>
inline bool applyPoolConfig(const PoolConfigT& config, const char* threadName = nullptr) noexcept {
  ThreadConfig tc{};
  tc.policy = config.policy;
  tc.priority = config.priority;
  tc.affinity = config.affinity;
  return applyThreadConfig(tc, threadName);
}

} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_THREAD_CONFIG_HPP
