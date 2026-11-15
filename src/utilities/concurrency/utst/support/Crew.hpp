#ifndef APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_CREW_HPP
#define APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_CREW_HPP
/**
 * @file Crew.hpp
 * @brief Threads of one test case and the state they share, owned so that the case ends in
 *        bounded time whatever the primitive under test does.
 *
 * A case that starts threads on a primitive must not join a thread the primitive never
 * releases, destroy a joinable thread on a fatal assertion, or free state a thread still uses.
 * A Crew holds the shared state on the heap, hands every thread a share of it, waits a bounded
 * time for the threads to end, and detaches the ones that have not: they keep the state alive
 * and the case reports the failure instead of hanging or aborting the test binary.
 */

#include "src/utilities/concurrency/utst/support/Waiting.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace apex {
namespace concurrency {
namespace test {

/// Base of every state a Crew shares. `abandoned` becomes true when the case has given up on
/// its threads; a thread body that spins while it waits for the case reads it and returns.
struct CrewState {
  std::atomic<bool> abandoned{false};
};

template <class State> class Crew {
  static_assert(std::is_base_of_v<CrewState, State>, "the shared state derives from CrewState");

public:
  template <class... Args>
  explicit Crew(Args&&... args) : shared_(std::make_shared<Shared>(std::forward<Args>(args)...)) {}

  Crew(const Crew&) = delete;
  Crew& operator=(const Crew&) = delete;

  /// A case left without finish(): a fatal assertion. Give up on the threads, then wait for the
  /// ones that can still end.
  ~Crew() {
    if (!threads_.empty()) {
      shared_->state.abandoned.store(true, std::memory_order_release);
      (void)finish();
    }
  }

  [[nodiscard]] State* operator->() noexcept { return &shared_->state; }
  [[nodiscard]] State& operator*() noexcept { return shared_->state; }

  /// Start a thread running `body(state)`.
  template <class Body> void start(Body body) {
    ++started_;
    threads_.emplace_back([shared = shared_, body = std::move(body)]() mutable {
      body(shared->state);
      shared->ended.fetch_add(1, std::memory_order_acq_rel);
    });
  }

  /// Wait up to `limit` for every started thread to end. True, with all of them joined, when
  /// they did. Otherwise the state is marked abandoned and the threads are detached.
  [[nodiscard]] bool finish(std::chrono::milliseconds limit = BOUND) {
    const bool ALL_ENDED = settles(
        [&]() { return shared_->ended.load(std::memory_order_acquire) == started_; }, limit);
    if (!ALL_ENDED) {
      shared_->state.abandoned.store(true, std::memory_order_release);
    }
    for (auto& thread : threads_) {
      if (ALL_ENDED) {
        thread.join();
      } else {
        thread.detach();
      }
    }
    threads_.clear();
    return ALL_ENDED;
  }

private:
  struct Shared {
    template <class... Args> explicit Shared(Args&&... args) : state(std::forward<Args>(args)...) {}
    State state;
    std::atomic<std::size_t> ended{0};
  };

  std::shared_ptr<Shared> shared_;
  std::vector<std::thread> threads_;
  std::size_t started_{0};
};

} // namespace test
} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_CREW_HPP
