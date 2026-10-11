/**
 * @file SchedulerRates_uTest.cpp
 * @brief Task-rate admission and cadence through the scheduler's own dispatch.
 *
 * Drives a single-thread scheduler cycle by cycle, the way the executive
 * does (tick index = cycle mod fundamental), counting each task's runs
 * and recording the cycles it ran on. Every rate in the rate contract is
 * checked by its delivered count and spacing, not by inspecting
 * bookkeeping.
 */

#include "src/system/core/components/scheduler/posix/inc/SchedulerSingleThread.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerStatus.hpp"
#include "src/system/core/components/scheduler/posix/inc/TaskConfig.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

using apex::concurrency::DelegateU8;
using system_core::scheduler::SchedulableTask;
using system_core::scheduler::SchedulerSingleThread;
using system_core::scheduler::Status;
using system_core::scheduler::TaskConfig;

namespace {

/// A task that records the cycles it ran on.
struct Recorder {
  std::vector<std::uint64_t> runs{};
  std::uint64_t cycle{0}; ///< Set by the driver before each dispatch.
};

std::uint8_t recordRun(void* ctx) noexcept {
  auto* r = static_cast<Recorder*>(ctx);
  r->runs.push_back(r->cycle);
  return 0;
}

std::filesystem::path logDir() {
  const auto DIR = std::filesystem::temp_directory_path() / "scheduler_rates_utest";
  std::filesystem::create_directories(DIR);
  return DIR;
}

/// Scheduler plus recorders, driven cycle by cycle.
class Harness {
public:
  explicit Harness(std::uint16_t hz) : hz_(hz), sched_(hz, logDir()) {
    EXPECT_EQ(sched_.init(), 0);
  }

  Status add(std::uint16_t n, std::uint16_t d, std::uint16_t offset) {
    recorders_.push_back(std::make_unique<Recorder>());
    tasks_.push_back(std::make_unique<SchedulableTask>(
        DelegateU8{&recordRun, static_cast<void*>(recorders_.back().get())}, "rate"));
    // Fields assigned directly: the constructor coerces a zero denominator
    // to one, and the refusal rule is under test here.
    TaskConfig cfg{};
    cfg.freqN = n;
    cfg.freqD = d;
    cfg.offset = offset;
    return sched_.addTask(*tasks_.back(), cfg);
  }

  /// Dispatch `cycles` consecutive cycles starting at the current one.
  void run(std::uint64_t cycles) {
    for (std::uint64_t i = 0; i < cycles; ++i) {
      for (auto& r : recorders_) {
        r->cycle = next_;
      }
      (void)sched_.executeTasksOnTickSingle(static_cast<std::uint16_t>(next_ % hz_));
      ++next_;
    }
  }

  const std::vector<std::uint64_t>& runs(std::size_t task = 0) const {
    return recorders_.at(task)->runs;
  }
  SchedulerSingleThread& scheduler() { return sched_; }

private:
  std::uint16_t hz_;
  SchedulerSingleThread sched_;
  std::vector<std::unique_ptr<Recorder>> recorders_{};
  std::vector<std::unique_ptr<SchedulableTask>> tasks_{};
  std::uint64_t next_{0};
};

/// Runs that fall inside [from, to).
std::uint64_t countIn(const std::vector<std::uint64_t>& runs, std::uint64_t from,
                      std::uint64_t to) {
  std::uint64_t n = 0;
  for (const auto R : runs) {
    if (R >= from && R < to) {
      ++n;
    }
  }
  return n;
}

} // namespace

/* ----------------------------- Admission ----------------------------- */

/** @test The refusal rule: zero fields, a rate above the fundamental, an offset outside the period.
 */
TEST(SchedulerRates, RefusalRule) {
  Harness h(100);
  EXPECT_EQ(h.add(0, 1, 0), Status::ERROR_FFREQ_MOD_TFREQN_DNE0);
  EXPECT_EQ(h.add(1, 0, 0), Status::ERROR_TFREQD_LT1);
  EXPECT_EQ(h.add(101, 1, 0), Status::ERROR_TFREQN_GT_FFREQ);
  EXPECT_EQ(h.add(201, 2, 0), Status::ERROR_TFREQN_GT_FFREQ);
  EXPECT_EQ(h.add(100, 1, 1), Status::ERROR_OFFSET_GTE_TPT);
  EXPECT_EQ(h.add(30, 1, 4), Status::ERROR_OFFSET_GTE_TPT); // period 3.33 ticks
  EXPECT_EQ(h.add(1, 4, 400), Status::ERROR_OFFSET_GTE_TPT);
  EXPECT_EQ(h.scheduler().taskCount(), 0U);
}

/** @test Rates that land on whole ticks and rates that do not are both admitted. */
TEST(SchedulerRates, AdmitsEvenAndUnevenRates) {
  Harness h(100);
  EXPECT_EQ(h.add(100, 1, 0), Status::SUCCESS);
  EXPECT_EQ(h.add(50, 1, 1), Status::SUCCESS);
  EXPECT_EQ(h.add(30, 1, 0), Status::SUCCESS); // 3.33 ticks: admitted, uneven
  EXPECT_EQ(h.add(7, 1, 2), Status::SUCCESS);
  EXPECT_EQ(h.add(1, 4, 150), Status::SUCCESS);
  EXPECT_EQ(h.add(200, 2, 0), Status::SUCCESS); // the fundamental, written as 200/2
  EXPECT_EQ(h.scheduler().taskCount(), 6U);
}

/* ----------------------------- Delivered rates ----------------------------- */

/** @test Every row of the rate table runs its configured count per pattern, three patterns over. */
TEST(SchedulerRates, CountPerPatternIsExact) {
  struct Case {
    std::uint16_t hz, n, d;
    const char* what;
  };
  const std::array<Case, 7> CASES{{{100, 1, 4, "0.25 Hz"},
                                   {1000, 100, 2, "50 Hz written as 100/2"},
                                   {100, 2, 4, "0.5 Hz written as 2/4"},
                                   {1000, 100, 3, "33.3 Hz written as 100/3"},
                                   {100, 30, 1, "30 Hz on 100 Hz"},
                                   {100, 100, 1, "every tick"},
                                   {1000, 1, 1, "1 Hz on 1000 Hz"}}};
  for (const auto& C : CASES) {
    Harness h(C.hz);
    ASSERT_EQ(h.add(C.n, C.d, 0), Status::SUCCESS) << C.what;
    const std::uint64_t PATTERN = static_cast<std::uint64_t>(C.hz) * C.d;
    h.run(3 * PATTERN);
    for (int rep = 0; rep < 3; ++rep) {
      EXPECT_EQ(countIn(h.runs(), rep * PATTERN, (rep + 1) * PATTERN), C.n)
          << C.what << " pattern " << rep;
    }
  }
}

/** @test 30 Hz on 100 Hz: 30 runs a second spaced 3, 3, 4 ticks, starting at the offset. */
TEST(SchedulerRates, UnevenRateSpacing) {
  Harness h(100);
  ASSERT_EQ(h.add(30, 1, 0), Status::SUCCESS);
  h.run(200);
  const auto& R = h.runs();
  ASSERT_EQ(R.size(), 60U);
  EXPECT_EQ(R[0], 0U);
  EXPECT_EQ(R[1], 3U);
  EXPECT_EQ(R[2], 6U);
  EXPECT_EQ(R[3], 10U);
  EXPECT_EQ(R[30], 100U);
  for (std::size_t i = 1; i < R.size(); ++i) {
    const std::uint64_t GAP = R[i] - R[i - 1];
    EXPECT_TRUE(GAP == 3U || GAP == 4U) << "gap " << GAP << " before run " << i;
  }
}

/** @test A whole-tick rate keeps a single spacing and its offset. */
TEST(SchedulerRates, EvenRateSpacingAndOffset) {
  Harness h(1000);
  ASSERT_EQ(h.add(100, 1, 5), Status::SUCCESS);
  h.run(1000);
  const auto& R = h.runs();
  ASSERT_EQ(R.size(), 100U);
  for (std::size_t i = 0; i < R.size(); ++i) {
    EXPECT_EQ(R[i], 5U + 10U * i);
  }
}

/** @test A multi-second period runs first at its offset, then every period, with no second-boundary
 * artefact. */
TEST(SchedulerRates, LongPeriodRunsAtOffset) {
  Harness h(100);
  ASSERT_EQ(h.add(1, 4, 150), Status::SUCCESS);
  h.run(1300);
  const auto& R = h.runs();
  ASSERT_EQ(R.size(), 3U);
  EXPECT_EQ(R[0], 150U);
  EXPECT_EQ(R[1], 550U);
  EXPECT_EQ(R[2], 950U);
}

/** @test Two tasks at the same rate with different offsets never share a cycle. */
TEST(SchedulerRates, OffsetsSeparateTasks) {
  Harness h(100);
  ASSERT_EQ(h.add(10, 1, 0), Status::SUCCESS);
  ASSERT_EQ(h.add(10, 1, 5), Status::SUCCESS);
  h.run(300);
  ASSERT_EQ(h.runs(0).size(), 30U);
  ASSERT_EQ(h.runs(1).size(), 30U);
  for (std::size_t i = 0; i < 30; ++i) {
    EXPECT_EQ(h.runs(0)[i], 10U * i);
    EXPECT_EQ(h.runs(1)[i], 10U * i + 5U);
  }
}

/* ----------------------------- Phase across a late add ----------------------------- */

/** @test A task added after cycles have run keeps the phase it would have had from cycle 0. */
TEST(SchedulerRates, LateAddKeepsAbsolutePhase) {
  Harness h(100);
  ASSERT_EQ(h.add(100, 1, 0), Status::SUCCESS); // something to dispatch while cycles pass
  h.run(250);
  ASSERT_EQ(h.add(1, 1, 30), Status::SUCCESS); // 1 Hz, offset 30: instants 30, 130, 230, 330, ...
  ASSERT_EQ(h.add(30, 1, 0), Status::SUCCESS); // uneven: 0, 3, 6, 10, ... 250, 253, 256, 260 ...
  h.run(200);
  const auto& ONE_HZ = h.runs(1);
  ASSERT_EQ(ONE_HZ.size(), 2U);
  EXPECT_EQ(ONE_HZ[0], 330U);
  EXPECT_EQ(ONE_HZ[1], 430U);
  const auto& THIRTY = h.runs(2);
  ASSERT_GE(THIRTY.size(), 4U);
  EXPECT_EQ(THIRTY[0], 250U);
  EXPECT_EQ(THIRTY[1], 253U);
  EXPECT_EQ(THIRTY[2], 256U);
  EXPECT_EQ(THIRTY[3], 260U);
  EXPECT_EQ(countIn(THIRTY, 250, 350), 30U);
}

/* ----------------------------- Period budget ----------------------------- */

/** @test The period used for the budget is the shortest gap, in nanoseconds of the fundamental. */
TEST(SchedulerRates, PeriodBudgetIsShortestGap) {
  Harness h(100);
  ASSERT_EQ(h.add(30, 1, 0), Status::SUCCESS);
  ASSERT_EQ(h.add(10, 1, 0), Status::SUCCESS);
  const auto& ENTRIES = h.scheduler().entries();
  ASSERT_EQ(ENTRIES.size(), 2U);
  EXPECT_EQ(ENTRIES[0].periodNs, 30'000'000U);  // 3 ticks of 10 ms
  EXPECT_EQ(ENTRIES[1].periodNs, 100'000'000U); // 10 ticks of 10 ms
}
