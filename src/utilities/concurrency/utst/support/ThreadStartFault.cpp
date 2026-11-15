/**
 * @file ThreadStartFault.cpp
 * @brief The interposed pthread_create behind ThreadStartFault.hpp.
 */

#include "src/utilities/concurrency/utst/support/ThreadStartFault.hpp"

#include <atomic>
#include <cerrno>

#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) ||                         \
    __has_feature(realtime_sanitizer)
#define APEX_THREAD_START_FAULT 0
#endif
#endif
#ifndef APEX_THREAD_START_FAULT
#define APEX_THREAD_START_FAULT 1
#endif
#else
#define APEX_THREAD_START_FAULT 0
#endif

#if APEX_THREAD_START_FAULT
#include <dlfcn.h>
#include <pthread.h>
#endif

namespace apex {
namespace concurrency {
namespace test {
namespace detail {

/// The seam's state: which creation fails and what has been seen since arming.
struct FaultState {
  std::atomic<unsigned> failAt{0}; ///< 0: disarmed, every creation passes through.
  std::atomic<unsigned> attempted{0};
  std::atomic<unsigned> started{0};
  std::atomic<unsigned> finished{0};
};

FaultState& faultState() noexcept {
  static FaultState state;
  return state;
}

} // namespace detail

bool threadStartFaultAvailable() noexcept { return APEX_THREAD_START_FAULT != 0; }

void armThreadStartFailure(unsigned callIndex) noexcept {
  detail::FaultState& state = detail::faultState();
  state.attempted.store(0);
  state.started.store(0);
  state.finished.store(0);
  state.failAt.store(callIndex);
}

void disarmThreadStartFailure() noexcept { detail::faultState().failAt.store(0); }

ThreadStartTally threadStartTally() noexcept {
  const detail::FaultState& state = detail::faultState();
  return {state.attempted.load(), state.started.load(), state.finished.load()};
}

} // namespace test
} // namespace concurrency
} // namespace apex

#if APEX_THREAD_START_FAULT

namespace {

/// The real start routine and its argument, run through a frame that counts
/// the routine's return.
struct StartFrame {
  void* (*routine)(void*);
  void* arg;
};

void* runCounted(void* raw) {
  const StartFrame FRAME = *static_cast<StartFrame*>(raw);
  delete static_cast<StartFrame*>(raw);
  void* const RESULT = FRAME.routine(FRAME.arg);
  apex::concurrency::test::detail::faultState().finished.fetch_add(1);
  return RESULT;
}

} // namespace

extern "C" int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                              void* (*routine)(void*), void* arg) {
  using Real = int (*)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
  static const Real realCreate = reinterpret_cast<Real>(dlsym(RTLD_NEXT, "pthread_create"));

  apex::concurrency::test::detail::FaultState& state =
      apex::concurrency::test::detail::faultState();
  const unsigned FAIL_AT = state.failAt.load();
  if (FAIL_AT == 0) {
    return realCreate(thread, attr, routine, arg);
  }
  if (state.attempted.fetch_add(1) + 1 == FAIL_AT) {
    return EAGAIN;
  }
  auto* frame = new StartFrame{routine, arg};
  const int RC = realCreate(thread, attr, &runCounted, frame);
  if (RC == 0) {
    state.started.fetch_add(1);
  } else {
    delete frame;
  }
  return RC;
}

#endif // APEX_THREAD_START_FAULT
