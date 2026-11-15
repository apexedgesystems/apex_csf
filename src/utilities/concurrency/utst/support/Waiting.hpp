#ifndef APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_WAITING_HPP
#define APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_WAITING_HPP
/**
 * @file Waiting.hpp
 * @brief Bounded waits for threaded tests: a condition that must come true
 *        within a limit, and a condition that must stay false, watched for a
 *        window. A case built on them fails where it would otherwise hang or
 *        pass by timing.
 */

#include <chrono>
#include <thread>

namespace apex {
namespace concurrency {
namespace test {

/// How long a case waits for something that must happen.
constexpr std::chrono::milliseconds BOUND{5000};

/// How long a case watches a thread that must be blocked.
constexpr std::chrono::milliseconds WATCH{20};

/// Poll `done` until it holds or `limit` passes.
template <class Pred> bool settles(Pred done, std::chrono::milliseconds limit = BOUND) {
  const auto DEADLINE = std::chrono::steady_clock::now() + limit;
  while (!done()) {
    if (std::chrono::steady_clock::now() > DEADLINE) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

/// Watch `escaped` for `window` and say whether it ever held. For a thread
/// that must be blocked: a correct primitive never lets it become true, so the
/// window costs time and cannot fail a correct build, while a primitive that
/// does not block lets the thread through well inside it.
template <class Pred> bool escapesWithin(Pred escaped, std::chrono::milliseconds window = WATCH) {
  const auto DEADLINE = std::chrono::steady_clock::now() + window;
  while (std::chrono::steady_clock::now() < DEADLINE) {
    if (escaped()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return escaped();
}

} // namespace test
} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_WAITING_HPP
