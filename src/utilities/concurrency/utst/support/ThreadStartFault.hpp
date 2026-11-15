#ifndef APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_THREAD_START_FAULT_HPP
#define APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_THREAD_START_FAULT_HPP
/**
 * @file ThreadStartFault.hpp
 * @brief Test seam at the thread-launch boundary: the test binary interposes
 *        pthread_create, so a chosen creation fails with EAGAIN while the
 *        earlier ones start for real and are tallied, including whether each
 *        ran to its end. Nothing in the library knows about it.
 *
 * The interposition is a definition of pthread_create in the executable,
 * which the dynamic linker binds ahead of libc's for every shared object in
 * the process. A sanitizer runtime (ASan, TSan, RTSan) owns that symbol
 * itself, so in those builds the seam is absent and
 * threadStartFaultAvailable() is false; tests skip there.
 */

namespace apex {
namespace concurrency {
namespace test {

/// What the interposed pthread_create saw while armed.
struct ThreadStartTally {
  unsigned attempted{0}; ///< Creations attempted, the failed one included.
  unsigned started{0};   ///< Creations that succeeded.
  unsigned finished{0};  ///< Started threads whose start routine has returned.
};

/// Whether this binary interposes pthread_create.
[[nodiscard]] bool threadStartFaultAvailable() noexcept;

/// From now on, the callIndex-th creation (1-based) fails with EAGAIN; the
/// others go through and are tallied. Resets the tally.
void armThreadStartFailure(unsigned callIndex) noexcept;

/// Creations go through untallied again.
void disarmThreadStartFailure() noexcept;

/// The tally since the last arm.
[[nodiscard]] ThreadStartTally threadStartTally() noexcept;

} // namespace test
} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_THREAD_START_FAULT_HPP
