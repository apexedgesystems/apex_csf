#ifndef APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_THREAD_PROBE_HPP
#define APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_THREAD_PROBE_HPP
/**
 * @file ThreadProbe.hpp
 * @brief What a thread reports about itself: the CPUs it may run on and its
 *        name. A task or thread body fills one in, and the test compares it
 *        with the configuration it asked for.
 */

#include <atomic>
#include <cstdint>
#include <cstring>

#include <pthread.h>
#include <sched.h>

namespace apex {
namespace concurrency {
namespace test {

/// The first CPU the calling thread may run on: a CPU a configuration can
/// name on any host, whatever cpuset the process was given. -1 if none.
inline int firstAllowedCpu() noexcept {
  cpu_set_t allowed;
  CPU_ZERO(&allowed);
  if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) {
    return -1;
  }
  for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
    if (CPU_ISSET(cpu, &allowed)) {
      return cpu;
    }
  }
  return -1;
}

/// A thread's own account of itself, published by `done`.
struct ThreadIdentity {
  std::atomic<bool> done{false};
  int allowedCpus{-1}; ///< How many CPUs the thread may run on.
  int onlyCpu{-1};     ///< That CPU when there is exactly one, else -1.
  char name[16]{};     ///< The thread's name.

  /// Fill from the calling thread.
  void recordHere() noexcept {
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (pthread_getaffinity_np(pthread_self(), sizeof(allowed), &allowed) == 0) {
      allowedCpus = CPU_COUNT(&allowed);
      if (allowedCpus == 1) {
        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
          if (CPU_ISSET(cpu, &allowed)) {
            onlyCpu = cpu;
          }
        }
      }
    }
    pthread_getname_np(pthread_self(), name, sizeof(name));
    done.store(true, std::memory_order_release);
  }

  /// The same, as a pool task: `ctx` is a ThreadIdentity.
  static std::uint8_t record(void* ctx) noexcept {
    static_cast<ThreadIdentity*>(ctx)->recordHere();
    return 0;
  }
};

} // namespace test
} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_THREAD_PROBE_HPP
