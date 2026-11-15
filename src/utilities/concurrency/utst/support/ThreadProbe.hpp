#ifndef APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_THREAD_PROBE_HPP
#define APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_THREAD_PROBE_HPP
/**
 * @file ThreadProbe.hpp
 * @brief What can be learned about a thread from outside the code under test:
 *        the CPUs it may run on and its name, which a task or thread body
 *        records about itself, and whether it is asleep in a futex wait on a
 *        given object, read from the kernel's record of its system call.
 */

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>

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
  int allowedCpus{-1};         ///< How many CPUs the thread may run on.
  int onlyCpu{-1};             ///< That CPU when there is exactly one, else -1.
  std::array<char, 16> name{}; ///< The thread's name.

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
    pthread_getname_np(pthread_self(), name.data(), name.size());
    done.store(true, std::memory_order_release);
  }

  /// The same, as a pool task: `ctx` is a ThreadIdentity.
  static std::uint8_t record(void* ctx) noexcept {
    static_cast<ThreadIdentity*>(ctx)->recordHere();
    return 0;
  }
};

/// How many of this process's threads are asleep in a futex wait on a word inside the `size`
/// bytes at `object`, read from each thread's /proc syscall record ("<number> <arguments in hex>
/// ..." while the thread is blocked in a system call). With `value`, only the threads waiting
/// against that expected word count. A thread counted here is inside the wait, past whatever its
/// code checked before deciding to block. -1 when the host does not expose the records.
inline int threadsAsleepIn(const void* object, std::size_t size,
                           std::optional<std::uint32_t> value = std::nullopt) {
  std::error_code failed;
  std::filesystem::directory_iterator tasks("/proc/self/task", failed);
  if (failed) {
    return -1;
  }
  const auto BEGIN = reinterpret_cast<std::uintptr_t>(object);
  bool readable = false;
  int asleep = 0;
  for (const auto& entry : tasks) {
    std::ifstream record(entry.path() / "syscall");
    std::string line;
    if (!std::getline(record, line)) {
      continue;
    }
    readable = true;
    unsigned long long number = 0;
    unsigned long long address = 0;
    unsigned long long operation = 0;
    unsigned long long expected = 0;
    if (std::sscanf(line.c_str(), "%llu %llx %llx %llx", &number, &address, &operation,
                    &expected) != 4) {
      continue; // "running", or a record without arguments
    }
    if (number == static_cast<unsigned long long>(SYS_futex) && address >= BEGIN &&
        address < BEGIN + size &&
        (!value.has_value() || static_cast<std::uint32_t>(expected) == *value)) {
      ++asleep;
    }
  }
  return readable ? asleep : -1;
}

} // namespace test
} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_THREAD_PROBE_HPP
