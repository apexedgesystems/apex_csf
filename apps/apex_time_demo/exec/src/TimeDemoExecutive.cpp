/**
 * @file TimeDemoExecutive.cpp
 * @brief TimeDemoExecutive implementation.
 */

#include "apps/apex_time_demo/exec/inc/TimeDemoExecutive.hpp"

#include "src/system/core/components/time_server/apex/inc/TimeServer.hpp"

#include <fmt/format.h>

#include <chrono>
#include <cstdint>

namespace appsim {
namespace exec {

namespace {

constexpr std::int64_t NS_PER_SEC = 1'000'000'000LL;

/// Reference UTC the GPS simulator hands TimeServer once it "acquires fix"
/// (3 seconds into the run). Picked as a recognizable Unix timestamp.
constexpr std::int64_t SIM_REFERENCE_EPOCH_NS = 1'700'000'000LL * NS_PER_SEC;

/// Seconds into the run when the GPS simulator triggers each scenario.
constexpr int SIM_FIX_AT_SEC = 3;
constexpr int SIM_DROPOUT_START_SEC = 18;
constexpr int SIM_RECOVERY_SEC = 25;

} // namespace

TimeDemoExecutive::~TimeDemoExecutive() {
  simRunning_.store(false, std::memory_order_release);
  if (simThread_.joinable()) {
    simThread_.join();
  }
}

/* ----------------------------- registerComponents ----------------------------- */

bool TimeDemoExecutive::registerComponents() noexcept {
  const auto& LOG_DIR = fileSystem().logDir();
  auto* log = sysLog();

  // Wire MockPps into the executive's TimeServer (already registered by the
  // base class). Init the source so readCapture works as soon as the GPS
  // simulator starts injecting edges.
  (void)pps_.init({});
  timeServer().setPpsSource(&pps_);

  // SystemMonitor is the canonical "is the app alive" support component.
  if (!registerComponent(&sysMonitor_, LOG_DIR)) {
    return false;
  }

  if (log != nullptr) {
    log->info("TIME_DEMO_EXEC",
              fmt::format("Registered: timeServer={:#x} sysmon={:#x}",
                          timeServer().fullUid(), sysMonitor_.fullUid()));
  }
  return true;
}

/* ----------------------------- configureComponents ----------------------------- */

void TimeDemoExecutive::configureComponents() noexcept {
  // The base ApexExecutive already wired actionComp.iface().timeProvider to
  // timeServer.utcTimeProvider(); ATS AT_TIME triggers consume UTC by
  // default. Nothing else to wire here at the executive level.

  // Spawn the GPS simulator thread. Captures `this` so it can mutate
  // pps_ and call timeServer().handleSetReferenceTime() / resetCorrelation.
  simRunning_.store(true, std::memory_order_release);
  simThread_ = std::thread(&TimeDemoExecutive::runGpsSimulator, this);

  if (auto* log = sysLog()) {
    log->info("TIME_DEMO_EXEC", "GPS simulator started.");
  }
}

/* ----------------------------- GPS simulator ----------------------------- */

void TimeDemoExecutive::runGpsSimulator() noexcept {
  using namespace std::chrono_literals;

  // Wall-clock start point so "seconds into the run" is well-defined.
  const auto t0 = std::chrono::steady_clock::now();
  auto secondsSinceStart = [&]() -> int {
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - t0)
            .count());
  };

  bool fixAcquired = false;
  bool inDropout = false;
  bool recovered = false;

  while (simRunning_.load(std::memory_order_acquire)) {
    const int s = secondsSinceStart();

    // 1) Cold-start dark period: nothing happens for the first SIM_FIX_AT_SEC
    //    seconds. valid stays NONE.

    // 2) GPS fix acquired -> push reference + start emitting PPS edges.
    if (!fixAcquired && s >= SIM_FIX_AT_SEC) {
      system_core::time_server::SetReferenceTime ref{};
      ref.epochNs = SIM_REFERENCE_EPOCH_NS;
      ref.source = static_cast<std::uint8_t>(system_core::time_server::TimeSource::GPS);
      ref.quality = static_cast<std::uint8_t>(system_core::time_server::TimeQuality::FINE);
      timeServer().handleSetReferenceTime(ref);
      fixAcquired = true;
      if (auto* log = sysLog()) {
        log->info("GPS_SIM", "Fix acquired; reference time delivered.");
      }
    }

    // 3) Dropout: skip edge injection while in the dropout window.
    if (!inDropout && s >= SIM_DROPOUT_START_SEC && !recovered) {
      inDropout = true;
      if (auto* log = sysLog()) {
        log->info("GPS_SIM", "Simulated PPS dropout begins.");
      }
    }

    // 4) Recovery: reset correlation and resume edges.
    if (inDropout && !recovered && s >= SIM_RECOVERY_SEC) {
      timeServer().resetCorrelation();
      system_core::time_server::SetReferenceTime ref{};
      ref.epochNs = SIM_REFERENCE_EPOCH_NS + (SIM_RECOVERY_SEC * NS_PER_SEC);
      ref.source = static_cast<std::uint8_t>(system_core::time_server::TimeSource::GPS);
      ref.quality = static_cast<std::uint8_t>(system_core::time_server::TimeQuality::FINE);
      timeServer().handleSetReferenceTime(ref);
      inDropout = false;
      recovered = true;
      if (auto* log = sysLog()) {
        log->info("GPS_SIM", "Recovery: resetCorrelation + fresh reference.");
      }
    }

    // Inject a PPS edge once per second, except during the dropout window.
    if (fixAcquired && !inDropout) {
      const auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count();
      pps_.injectEdge(static_cast<std::int64_t>(nowNs));
    }

    std::this_thread::sleep_for(1s);
  }
}

} // namespace exec
} // namespace appsim
