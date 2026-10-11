/**
 * @file TaskCadence_uTest.cpp
 * @brief Unit tests for the task run-instant arithmetic.
 */

#include "src/system/core/components/scheduler/posix/inc/TaskCadence.hpp"

#include <array>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

using system_core::scheduler::TaskCadence;

namespace {

/// Run instants by the defining formula, for cross-checking the stepper.
std::uint64_t formulaRun(std::uint32_t hz, std::uint32_t n, std::uint32_t d, std::uint32_t offset,
                         std::uint64_t k) {
  const std::uint64_t HD = static_cast<std::uint64_t>(hz) * d;
  return offset + (k * HD) / n;
}

/// First `runs` instants from the stepper.
std::vector<std::uint64_t> stepped(std::uint32_t hz, std::uint32_t n, std::uint32_t d,
                                   std::uint32_t offset, std::size_t runs) {
  TaskCadence c{};
  c.init(hz, n, d, offset);
  std::vector<std::uint64_t> out;
  out.reserve(runs);
  for (std::size_t i = 0; i < runs; ++i) {
    out.push_back(c.nextRun);
    c.advance();
  }
  return out;
}

} // namespace

/* ----------------------------- Admission ----------------------------- */

/** @test Zero numerator, zero denominator and a zero fundamental are refused. */
TEST(TaskCadence, RefusesZeros) {
  EXPECT_FALSE(TaskCadence::admissible(100, 0, 1, 0));
  EXPECT_FALSE(TaskCadence::admissible(100, 1, 0, 0));
  EXPECT_FALSE(TaskCadence::admissible(0, 1, 1, 0));
}

/** @test A rate above the fundamental is refused; the fundamental itself is admitted. */
TEST(TaskCadence, RefusesRatesAboveFundamental) {
  EXPECT_TRUE(TaskCadence::admissible(100, 100, 1, 0));
  EXPECT_FALSE(TaskCadence::admissible(100, 101, 1, 0));
  EXPECT_TRUE(TaskCadence::admissible(100, 200, 2, 0)); // 100 Hz written as 200/2
  EXPECT_FALSE(TaskCadence::admissible(100, 201, 2, 0));
}

/** @test The offset must sit inside the first period: offset * N < Hz * D. */
TEST(TaskCadence, OffsetInsidePeriod) {
  EXPECT_TRUE(TaskCadence::admissible(1000, 100, 1, 9));
  EXPECT_FALSE(TaskCadence::admissible(1000, 100, 1, 10));
  EXPECT_TRUE(TaskCadence::admissible(100, 30, 1, 3));  // period 3.33: 3 * 30 = 90 < 100
  EXPECT_FALSE(TaskCadence::admissible(100, 30, 1, 4)); // 4 * 30 = 120
  EXPECT_TRUE(TaskCadence::admissible(100, 1, 4, 399)); // period 400 ticks
  EXPECT_FALSE(TaskCadence::admissible(100, 1, 4, 400));
  EXPECT_TRUE(TaskCadence::admissible(100, 100, 1, 0)); // every tick: only offset 0
  EXPECT_FALSE(TaskCadence::admissible(100, 100, 1, 1));
}

/** @test Rates that do not land on whole ticks are admitted. */
TEST(TaskCadence, AdmitsUnevenRates) {
  EXPECT_TRUE(TaskCadence::admissible(100, 30, 1, 0));
  EXPECT_TRUE(TaskCadence::admissible(1000, 100, 3, 0));
  EXPECT_TRUE(TaskCadence::admissible(100, 7, 1, 0));
}

/* ----------------------------- Stepping ----------------------------- */

/** @test The stepper reproduces the defining formula for a spread of rates. */
TEST(TaskCadence, StepperMatchesFormula) {
  struct Case {
    std::uint32_t hz, n, d, offset;
  };
  const std::array<Case, 11> CASES{{{100, 100, 1, 0},
                                    {100, 1, 1, 0},
                                    {100, 30, 1, 0},
                                    {100, 30, 1, 3},
                                    {1000, 100, 2, 0},
                                    {100, 2, 4, 0},
                                    {1000, 100, 3, 0},
                                    {100, 1, 4, 150},
                                    {100, 7, 1, 2},
                                    {1000, 999, 1, 0},
                                    {1000, 1, 65535, 0}}};
  for (const auto& C : CASES) {
    ASSERT_TRUE(TaskCadence::admissible(C.hz, C.n, C.d, C.offset));
    const auto RUNS = stepped(C.hz, C.n, C.d, C.offset, 5000);
    for (std::size_t k = 0; k < RUNS.size(); ++k) {
      ASSERT_EQ(RUNS[k], formulaRun(C.hz, C.n, C.d, C.offset, k))
          << "hz=" << C.hz << " n=" << C.n << " d=" << C.d << " k=" << k;
    }
  }
}

/** @test The rate table from the README: every row runs the configured count per pattern. */
TEST(TaskCadence, CountPerPatternIsExact) {
  struct Case {
    std::uint32_t hz, n, d;
    const char* what;
  };
  const std::array<Case, 5> CASES{{{100, 1, 4, "0.25 Hz"},
                                   {1000, 100, 2, "50 Hz written as 100/2"},
                                   {100, 2, 4, "0.5 Hz written as 2/4"},
                                   {1000, 100, 3, "33.3 Hz written as 100/3"},
                                   {100, 30, 1, "30 Hz on 100 Hz"}}};
  for (const auto& C : CASES) {
    const std::uint64_t PATTERN = TaskCadence::patternTicks(C.hz, C.d);
    TaskCadence c{};
    c.init(C.hz, C.n, C.d, 0);
    // Count runs in three consecutive patterns by walking every cycle.
    for (int rep = 0; rep < 3; ++rep) {
      std::uint64_t runs = 0;
      for (std::uint64_t cycle = rep * PATTERN; cycle < (rep + 1) * PATTERN; ++cycle) {
        if (c.due(cycle)) {
          ++runs;
          c.advance();
        }
      }
      EXPECT_EQ(runs, C.n) << C.what << " pattern " << rep;
    }
  }
}

/** @test 30 Hz on 100 Hz: spacing 3, 3, 4 repeating, 30 runs a second. */
TEST(TaskCadence, ThirtyOnHundredSpacing) {
  const auto RUNS = stepped(100, 30, 1, 0, 31);
  EXPECT_EQ(RUNS[0], 0U);
  EXPECT_EQ(RUNS[1], 3U);
  EXPECT_EQ(RUNS[2], 6U);
  EXPECT_EQ(RUNS[3], 10U);
  EXPECT_EQ(RUNS[30], 100U);
  TaskCadence c{};
  c.init(100, 30, 1, 0);
  EXPECT_TRUE(c.uneven());
  EXPECT_EQ(c.shortestGapTicks(), 3U);
  EXPECT_EQ(c.longestGapTicks(), 4U);
}

/** @test A whole-tick rate has one spacing and is not uneven. */
TEST(TaskCadence, WholeTickRateIsEven) {
  TaskCadence c{};
  c.init(1000, 100, 1, 5);
  EXPECT_FALSE(c.uneven());
  EXPECT_EQ(c.shortestGapTicks(), 10U);
  EXPECT_EQ(c.longestGapTicks(), 10U);
  const auto RUNS = stepped(1000, 100, 1, 5, 4);
  EXPECT_EQ(RUNS[0], 5U);
  EXPECT_EQ(RUNS[1], 15U);
  EXPECT_EQ(RUNS[3], 35U);
}

/** @test A multi-second period runs first at its offset, then every period. */
TEST(TaskCadence, LongPeriodFirstRunAtOffset) {
  const auto RUNS = stepped(100, 1, 4, 150, 3);
  EXPECT_EQ(RUNS[0], 150U);
  EXPECT_EQ(RUNS[1], 550U);
  EXPECT_EQ(RUNS[2], 950U);
}

/** @test The slowest rate: 1/65535 Hz at 1000 Hz is one run every 65,535,000 ticks. */
TEST(TaskCadence, SlowestRate) {
  TaskCadence c{};
  c.init(1000, 1, 65535, 0);
  EXPECT_EQ(c.whole, 65'535'000U);
  EXPECT_FALSE(c.uneven());
  EXPECT_EQ(c.nextRun, 0U);
  c.advance();
  EXPECT_EQ(c.nextRun, 65'535'000U);
  c.advance();
  EXPECT_EQ(c.nextRun, 131'070'000U);
}

/* ----------------------------- Grid alignment at load ----------------------------- */

/** @test A table loaded mid-run keeps the phase it would have had from cycle 0. */
TEST(TaskCadence, InitAtKeepsAbsolutePhase) {
  struct Case {
    std::uint32_t hz, n, d, offset;
  };
  const std::array<Case, 5> CASES{
      {{100, 100, 1, 0}, {100, 30, 1, 3}, {1000, 100, 3, 0}, {100, 1, 4, 150}, {100, 7, 1, 2}}};
  const std::array<std::uint64_t, 9> LOADS{0, 1, 149, 150, 151, 999, 1000, 123'456, 7'000'003};
  for (const auto& C : CASES) {
    for (const std::uint64_t LOAD : LOADS) {
      TaskCadence fresh{};
      fresh.init(C.hz, C.n, C.d, C.offset);
      while (fresh.nextRun < LOAD) {
        fresh.advance();
      }
      TaskCadence late{};
      late.initAt(C.hz, C.n, C.d, C.offset, LOAD);
      EXPECT_EQ(late.nextRun, fresh.nextRun) << "n=" << C.n << " d=" << C.d << " load=" << LOAD;
      EXPECT_EQ(late.acc, fresh.acc) << "n=" << C.n << " d=" << C.d << " load=" << LOAD;
      // And they stay in step afterwards.
      for (int i = 0; i < 50; ++i) {
        late.advance();
        fresh.advance();
        ASSERT_EQ(late.nextRun, fresh.nextRun);
      }
    }
  }
}

/** @test Loading exactly on a run instant keeps that instant as the first run. */
TEST(TaskCadence, InitAtOnInstantRunsNow) {
  TaskCadence c{};
  c.initAt(100, 1, 4, 150, 550);
  EXPECT_EQ(c.nextRun, 550U);
  EXPECT_TRUE(c.due(550));
}

/* ----------------------------- Due and advance ----------------------------- */

/** @test due() is at-or-after, and one advance restores the lead when no cycle is skipped. */
TEST(TaskCadence, DueAndAdvance) {
  TaskCadence c{};
  c.init(100, 30, 1, 0);
  EXPECT_TRUE(c.due(0));
  EXPECT_TRUE(c.due(1)); // never happens when every cycle is dispatched; stays runnable
  c.advance();
  EXPECT_FALSE(c.due(1));
  EXPECT_FALSE(c.due(2));
  EXPECT_TRUE(c.due(3));
}
