/**
 * @file ExecutiveCadence_pTest.cpp
 * @brief Cadence baseline for the executive clock: period fidelity and drift.
 *
 * Boots a real ApexExecutive (config-less CLI boot, no components) and
 * measures the delivered tick cadence from outside, the way a paired
 * consumer experiences it: a sampler thread polls the health packet's
 * clock cycle counter and timestamps every observed cycle edge. From
 * the edge series it reports:
 *
 *  - mean period and its error against the configured frame period
 *  - min / max / p95 / p99 observed periods (jitter tail)
 *  - cumulative drift: (last_edge - first_edge) - (cycles * period),
 *    the number the absolute deadline grid pins near zero regardless
 *    of per-tick oversleep
 *  - resyncs and end-state clock/task parity as sanity gates
 *
 * This is deliberately not a PerfCase throughput loop: a paced 100 Hz
 * loop's calls-per-second is fixed by construction and meaningless as
 * a metric. Cadence quality lives in the period distribution and the
 * drift, so those are measured and reported directly.
 *
 * Polling at ~0.2 ms quantizes each edge to well under 5% of a 10 ms
 * frame — adequate for millisecond-scale jitter tails and exact for
 * drift (which only needs the first and last edges).
 *
 * Environment knobs:
 *   CADENCE_CYCLES  total clock cycles to run (default 1000 = 10 s)
 *   CADENCE_CSV     if set, append "cycle,edge_ns" samples to this path
 */

#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace {

using executive::ApexExecutive;
using Clock = std::chrono::steady_clock;

constexpr double FRAME_MS = 10.0; // default executive clock: 100 Hz

class CadenceExecutive : public ApexExecutive {
public:
  CadenceExecutive(const std::filesystem::path& fsRoot, std::uint64_t targetCycles)
      : ApexExecutive("ExecutiveCadence_pTest",
                      {"--shutdown-mode", "cycle", "--shutdown-cycle", std::to_string(targetCycles),
                       "--rt-mode", "lag-tolerant", "--rt-max-lag", "1000000", "--skip-cleanup",
                       "--verbosity", "0"},
                      fsRoot) {}
};

std::uint64_t envCycles() noexcept {
  const char* s = std::getenv("CADENCE_CYCLES");
  return (s && *s != '\0') ? std::strtoull(s, nullptr, 10) : 1000u;
}

TEST(ExecutiveCadencePerf, PeriodFidelityAndDrift) {
  const std::uint64_t CYCLES = envCycles();
  const auto FS_ROOT = std::filesystem::path(::testing::TempDir()) / "exec_cadence_ptst";
  std::filesystem::remove_all(FS_ROOT);
  std::filesystem::create_directories(FS_ROOT);

  CadenceExecutive exec(FS_ROOT, CYCLES);
  ASSERT_EQ(exec.init(), 0);

  // Edge sampler: one wall timestamp per observed cycle-count change.
  std::vector<std::uint64_t> edgeCycle;
  std::vector<std::int64_t> edgeNs;
  edgeCycle.reserve(CYCLES + 8);
  edgeNs.reserve(CYCLES + 8);
  std::atomic<bool> stopSampler{false};

  std::thread sampler([&]() {
    std::uint64_t last = 0;
    while (!stopSampler.load(std::memory_order_acquire)) {
      const std::uint64_t C = exec.getHealthPacket().clockCycles;
      if (C != last) {
        edgeCycle.push_back(C);
        edgeNs.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
                .count());
        last = C;
      }
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
  });

  std::thread runner([&]() { static_cast<void>(exec.run()); });
  runner.join();
  stopSampler.store(true, std::memory_order_release);
  sampler.join();

  ASSERT_GE(edgeCycle.size(), CYCLES / 2) << "sampler missed most edges";

  // Period series between consecutive single-step edges. Multi-step
  // jumps (sampler overrun during a catch-up burst) are excluded from
  // the period distribution but still bounded by the drift metric.
  std::vector<double> periodsMs;
  periodsMs.reserve(edgeCycle.size());
  for (std::size_t i = 1; i < edgeCycle.size(); ++i) {
    if (edgeCycle[i] == edgeCycle[i - 1] + 1) {
      periodsMs.push_back(static_cast<double>(edgeNs[i] - edgeNs[i - 1]) / 1e6);
    }
  }
  ASSERT_GE(periodsMs.size(), CYCLES / 2) << "too few single-step periods";

  double sum = 0.0;
  for (const double P : periodsMs) {
    sum += P;
  }
  const double MEAN = sum / static_cast<double>(periodsMs.size());

  std::vector<double> sorted = periodsMs;
  std::sort(sorted.begin(), sorted.end());
  const auto PCT = [&sorted](double q) {
    const auto IDX = static_cast<std::size_t>(q * static_cast<double>(sorted.size() - 1));
    return sorted[IDX];
  };

  // Drift over the whole observed window: how far the delivered grid
  // diverged from the ideal one anchored at the first observed edge.
  const auto SPAN_CYCLES = static_cast<double>(edgeCycle.back() - edgeCycle.front());
  const double SPAN_MS = static_cast<double>(edgeNs.back() - edgeNs.front()) / 1e6;
  const double DRIFT_MS = SPAN_MS - SPAN_CYCLES * FRAME_MS;

  const auto& HP = exec.getHealthPacket();

  std::printf("ExecutiveCadencePerf.PeriodFidelityAndDrift: cycles=%llu edges=%zu "
              "period mean=%.4f ms (err %+.4f) min=%.3f p95=%.3f p99=%.3f max=%.3f | "
              "drift=%+.3f ms over %.1f s | overruns=%llu resyncDropped=%u\n",
              static_cast<unsigned long long>(HP.clockCycles), edgeCycle.size(), MEAN,
              MEAN - FRAME_MS, sorted.front(), PCT(0.95), PCT(0.99), sorted.back(), DRIFT_MS,
              SPAN_MS / 1000.0, static_cast<unsigned long long>(HP.frameOverrunCount),
              HP.resyncDroppedTicks);

  if (const char* csv = std::getenv("CADENCE_CSV")) {
    if (std::FILE* f = std::fopen(csv, "a")) {
      for (std::size_t i = 0; i < edgeCycle.size(); ++i) {
        std::fprintf(f, "%llu,%lld\n", static_cast<unsigned long long>(edgeCycle[i]),
                     static_cast<long long>(edgeNs[i]));
      }
      std::fclose(f);
    }
  }

  // Sanity gates, deliberately loose: the baseline artifact carries the
  // real numbers; these only catch a broken grid (mean off by >2% or
  // drift growing with run length) on an otherwise-idle timing rail.
  EXPECT_NEAR(MEAN, FRAME_MS, 0.2);
  EXPECT_LT(std::abs(DRIFT_MS), 50.0);
  EXPECT_EQ(HP.resyncDroppedTicks, 0u);
  EXPECT_EQ(HP.clockCycles, CYCLES);

  std::filesystem::remove_all(FS_ROOT);
}

} // namespace
