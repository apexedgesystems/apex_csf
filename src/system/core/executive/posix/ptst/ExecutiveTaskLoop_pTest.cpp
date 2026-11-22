/**
 * @file ExecutiveTaskLoop_pTest.cpp
 * @brief CPU cost of the executive's task loop per delivered cycle.
 *
 * Boots a real ApexExecutive, once with nothing scheduled and once with
 * one registered model whose single no-work task a schedule file
 * dispatches every cycle. A hook the task thread calls once per cycle
 * reads that thread's own CPU clock, so the difference between two
 * consecutive readings covers every instruction the thread spent on one
 * turn of its loop: the wake, the command drain, the dispatch with its
 * component lookup and lock test, the in-flight wait, the action phase
 * and the bookkeeping. Time asleep is not CPU time; the short spin at the
 * start of the in-flight wait is. The clock read itself is part of every
 * sample, the same in any two builds compared.
 *
 * Reported per case: the per-cycle samples' median, 10th and 90th
 * percentile, minimum and maximum in nanoseconds, their sum, the cycles
 * measured and, for the scheduled case, the task runs delivered in the
 * window. The delivered runs must be within one of the cycles measured:
 * a dropped or skipped dispatch would show there.
 *
 * This is deliberately not a PerfCase throughput loop: the loop under
 * measurement is paced by the executive's clock, so calls per second is
 * fixed by construction.
 *
 * Environment knobs:
 *   TASKLOOP_CYCLES  measured cycles (default 1000)
 *   TASKLOOP_WARMUP  cycles run before the first sample (default 100)
 *   TASKLOOP_HZ      fundamental frequency (default 100)
 *   TASKLOOP_CSV     if set, append "case,cycle,cpu_ns" samples to this path
 */

#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerData.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SystemComponentBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/TprmPayload.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <time.h>

namespace {

using executive::ApexExecutive;

std::uint64_t envNumber(const char* name, std::uint64_t fallback) noexcept {
  const char* s = std::getenv(name);
  return (s != nullptr && *s != '\0') ? std::strtoull(s, nullptr, 10) : fallback;
}

/// CPU time the calling thread has used, in nanoseconds.
std::uint64_t threadCpuNs() noexcept {
  timespec ts{};
  static_cast<void>(::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts));
  return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000U +
         static_cast<std::uint64_t>(ts.tv_nsec);
}

/// A model with one task that counts its runs and does nothing else.
class NoWorkModel final : public system_core::system_component::SwModelBase {
public:
  static constexpr std::uint16_t COMPONENT_ID = 240;
  static constexpr std::uint8_t STEP_TASK_UID = 1;

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return "NoWorkModel"; }
  [[nodiscard]] const char* label() const noexcept override { return "NO_WORK"; }

  [[nodiscard]] std::uint8_t step() noexcept {
    runs.fetch_add(1, std::memory_order_relaxed);
    return 0;
  }

  std::atomic<std::uint64_t> runs{0};

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    return registerTask<NoWorkModel, &NoWorkModel::step>(STEP_TASK_UID, this, "step") ? 0 : 1;
  }
};

/// Executive that samples its task thread's CPU clock once per cycle.
class LoopExecutive final : public ApexExecutive {
public:
  LoopExecutive(const std::filesystem::path& fsRoot, std::uint64_t warmup, std::uint64_t measured,
                std::uint32_t hz, bool scheduled)
      : ApexExecutive("ExecutiveTaskLoop_pTest",
                      {"--shutdown-mode", "cycle", "--shutdown-cycle",
                       std::to_string(warmup + measured + 20U), "--clock-hz", std::to_string(hz),
                       "--rt-mode", "lag-tolerant", "--rt-max-lag", "1000000", "--skip-cleanup",
                       "--startup-delay", "0", "--verbosity", "0"},
                      fsRoot),
        warmup_(warmup), measured_(measured), scheduled_(scheduled) {
    samples_.reserve(measured + 1U);
  }

  [[nodiscard]] std::filesystem::path tprmDir() noexcept { return fileSystem().tprmDir(); }

  /// One CPU-time sample per measured cycle, in the order taken.
  [[nodiscard]] const std::vector<std::uint64_t>& samples() const noexcept { return samples_; }
  /// Task runs between the first and the last sample.
  [[nodiscard]] std::uint64_t runsDelivered() const noexcept { return runsEnd_ - runsStart_; }

protected:
  [[nodiscard]] bool registerComponents() noexcept override {
    return !scheduled_ || registerComponent(&model_, fileSystem().logDir());
  }

  void onWaitDeadlineSelected(std::uint64_t taskCycles, std::uint64_t) noexcept override {
    if (taskCycles < warmup_ || taskCycles > warmup_ + measured_) {
      return;
    }
    const std::uint64_t NOW = threadCpuNs();
    if (taskCycles == warmup_) {
      runsStart_ = model_.runs.load(std::memory_order_relaxed);
    } else {
      samples_.push_back(NOW - last_);
    }
    if (taskCycles == warmup_ + measured_) {
      runsEnd_ = model_.runs.load(std::memory_order_relaxed);
    }
    last_ = NOW;
  }

private:
  NoWorkModel model_;
  std::uint64_t warmup_;
  std::uint64_t measured_;
  bool scheduled_;
  std::uint64_t last_{0};
  std::uint64_t runsStart_{0};
  std::uint64_t runsEnd_{0};
  std::vector<std::uint64_t> samples_;
};

/// Write the scheduler's parameter file into DIR: the model's task on every cycle.
bool writeSchedule(const std::filesystem::path& dir, std::uint32_t hz) {
  namespace sc = system_core::system_component;
  namespace sch = system_core::scheduler;
  constexpr std::uint32_t SCHEDULER_UID = 1U << 8;

  sch::SchedulerTprmHeader header{};
  header.numPools = 1;
  header.workersPerPool = 2;
  header.numTasks = 1;
  sch::SchedulerTaskEntry step{};
  step.fullUid = static_cast<std::uint32_t>(NoWorkModel::COMPONENT_ID) << 8;
  step.taskUid = NoWorkModel::STEP_TASK_UID;
  step.freqN = static_cast<std::uint16_t>(hz);
  step.freqD = 1;
  step.sequenceGroup = 0xFF;

  std::array<std::uint8_t, sizeof(header) + sizeof(step)> image{};
  std::memcpy(image.data(), &header, sizeof(header));
  std::memcpy(image.data() + sizeof(header), &step, sizeof(step));

  sc::TprmPayloadHeader prelude{};
  prelude.magic = sc::TPRM_PAYLOAD_MAGIC;
  prelude.version = sc::TPRM_PAYLOAD_VERSION;
  prelude.fullUid = SCHEDULER_UID;
  prelude.payloadSize = image.size();
  prelude.payloadCrc = sc::tprmCrc32(image.data(), image.size());

  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::ofstream out(dir / sc::SystemComponentBase::tprmFilename(SCHEDULER_UID),
                    std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(&prelude), sizeof(prelude));
  out.write(reinterpret_cast<const char*>(image.data()),
            static_cast<std::streamsize>(image.size()));
  out.flush();
  return out.good();
}

/// Run one case and print its figures.
void measure(const char* name, bool scheduled) {
  const std::uint64_t MEASURED = envNumber("TASKLOOP_CYCLES", 1000U);
  const std::uint64_t WARMUP = envNumber("TASKLOOP_WARMUP", 100U);
  const auto HZ = static_cast<std::uint32_t>(envNumber("TASKLOOP_HZ", 100U));
  const auto FS_ROOT =
      std::filesystem::path(::testing::TempDir()) / (std::string("exec_taskloop_ptst_") + name);
  std::filesystem::remove_all(FS_ROOT);
  std::filesystem::create_directories(FS_ROOT);

  LoopExecutive exec(FS_ROOT, WARMUP, MEASURED, HZ, scheduled);
  if (scheduled) {
    ASSERT_TRUE(writeSchedule(exec.tprmDir(), HZ));
  }
  ASSERT_EQ(exec.init(), 0);
  std::thread runner([&]() { static_cast<void>(exec.run()); });
  runner.join();

  const std::vector<std::uint64_t>& SAMPLES = exec.samples();
  ASSERT_EQ(SAMPLES.size(), MEASURED) << "the task thread did not run every measured cycle";
  std::vector<std::uint64_t> sorted = SAMPLES;
  std::sort(sorted.begin(), sorted.end());
  const auto PCT = [&sorted](double q) {
    return sorted[static_cast<std::size_t>(q * static_cast<double>(sorted.size() - 1))];
  };
  std::uint64_t sum = 0;
  for (const std::uint64_t S : SAMPLES) {
    sum += S;
  }

  std::printf(
      "ExecutiveTaskLoopPerf.%s: hz=%u cycles=%llu cpu_ns_per_cycle median=%llu "
      "p10=%llu p90=%llu min=%llu max=%llu sum=%llu runs=%llu\n",
      name, HZ, static_cast<unsigned long long>(MEASURED),
      static_cast<unsigned long long>(PCT(0.50)), static_cast<unsigned long long>(PCT(0.10)),
      static_cast<unsigned long long>(PCT(0.90)), static_cast<unsigned long long>(sorted.front()),
      static_cast<unsigned long long>(sorted.back()), static_cast<unsigned long long>(sum),
      static_cast<unsigned long long>(exec.runsDelivered()));

  if (const char* csv = std::getenv("TASKLOOP_CSV")) {
    if (std::FILE* f = std::fopen(csv, "a")) {
      for (std::size_t i = 0; i < SAMPLES.size(); ++i) {
        std::fprintf(f, "%s,%zu,%llu\n", name, i, static_cast<unsigned long long>(SAMPLES[i]));
      }
      std::fclose(f);
    }
  }

  if (scheduled) {
    // One dispatch per cycle; the run of the last sampled cycle may still
    // be on its worker when the hook reads the count.
    EXPECT_GE(exec.runsDelivered() + 1U, MEASURED);
    EXPECT_LE(exec.runsDelivered(), MEASURED + 1U);
  } else {
    EXPECT_EQ(exec.runsDelivered(), 0U);
  }
  std::filesystem::remove_all(FS_ROOT);
}

} // namespace

/** @brief The task loop with nothing scheduled: its fixed cost per cycle. */
TEST(ExecutiveTaskLoopPerf, EmptyLoop) { measure("EmptyLoop", false); }

/** @brief The task loop dispatching one registered component's no-work task every cycle. */
TEST(ExecutiveTaskLoopPerf, OneScheduledTask) { measure("OneScheduledTask", true); }
