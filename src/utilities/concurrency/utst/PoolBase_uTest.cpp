/**
 * @file PoolBase_uTest.cpp
 * @brief Unit tests for concurrency::PoolBase: the error record, the
 *        in-flight sweep over the workers' tallies and the tally each worker
 *        is started with.
 */

#include "src/utilities/concurrency/inc/PoolBase.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <functional>
#include <limits>
#include <mutex>
#include <type_traits>
#include <vector>

using apex::concurrency::anyTaskInFlight;
using apex::concurrency::PoolBase;
using apex::concurrency::PoolConfig;
using apex::concurrency::PoolStatus;
using apex::concurrency::TaskError;
using apex::concurrency::WorkerTally;

/* ----------------------------- TaskError Compile-Time Contract ----------------------------- */

// A plain record a worker fills in and a collector copies out: no constructor, copied bit for
// bit, and all zero when value-initialized.
static_assert(std::is_aggregate_v<TaskError>);
static_assert(std::is_trivially_copyable_v<TaskError>);
static_assert(TaskError{}.label == nullptr && TaskError{}.errorCode == 0 &&
              TaskError{}.status == PoolStatus::SUCCESS);

namespace {

/// What happens to the tallies while a sweep is under way: `step` runs once,
/// right after the `after`-th count of the whole set has been read.
struct Script {
  std::size_t after{0};
  std::function<void()> step;
  std::size_t loads{0};
};

/// A count with the one operation the sweep uses, reading under a Script.
struct ScriptedCount {
  std::size_t value{0};
  Script* script{nullptr};

  std::size_t load(std::memory_order /*order*/) const {
    const std::size_t SEEN = value;
    if (++script->loads == script->after) {
      script->step();
    }
    return SEEN;
  }
};

struct ScriptedTally {
  ScriptedCount started;
  ScriptedCount finished;
};

/// Two workers under one script, each with `started` and `finished` given.
std::array<ScriptedTally, 2> twoWorkers(Script& script, std::size_t startedA, std::size_t finishedA,
                                        std::size_t startedB, std::size_t finishedB) {
  return {ScriptedTally{{startedA, &script}, {finishedA, &script}},
          ScriptedTally{{startedB, &script}, {finishedB, &script}}};
}

void setTally(WorkerTally& tally, std::size_t started, std::size_t finished) {
  tally.started.store(started);
  tally.finished.store(finished);
}

/// A pool whose workers record the tally they were started with and return.
class TallyRecorder : public PoolBase {
public:
  explicit TallyRecorder(std::size_t workers) : PoolBase(workers) {
    allStarted_ = spawnWorkers(PoolConfig{}, "tally_w", &TallyRecorder::record, this);
    joinWorkers();
  }

  [[nodiscard]] bool allStarted() const noexcept { return allStarted_; }
  [[nodiscard]] const std::vector<const WorkerTally*>& seen() const noexcept { return seen_; }

private:
  static void record(void* pool, WorkerTally& tally) {
    auto* self = static_cast<TallyRecorder*>(pool);
    const std::lock_guard<std::mutex> LOCK(self->mutex_);
    self->seen_.push_back(&tally);
  }

  bool allStarted_{false};
  std::mutex mutex_;
  std::vector<const WorkerTally*> seen_;
};

} // namespace

/* ----------------------------- In-flight Sweep Tests ----------------------------- */

/** @test Workers whose started and finished counts agree have nothing in flight, at any count,
 * and so has a pool with no workers. */
TEST(PoolBaseInFlight, EqualCountsAreIdle) {
  std::array<WorkerTally, 3> workers;
  setTally(workers[0], 0, 0);
  setTally(workers[1], 5, 5);
  setTally(workers[2], std::numeric_limits<std::size_t>::max(),
           std::numeric_limits<std::size_t>::max());

  EXPECT_FALSE(anyTaskInFlight(workers.data(), workers.size()));
  EXPECT_FALSE(anyTaskInFlight(workers.data(), 0));
}

/** @test One worker with a task started and not finished puts a task in flight. So does a
 * started count that has wrapped to zero ahead of its finished count: the sums are compared for
 * a difference, and the started sum is then the smaller number. */
TEST(PoolBaseInFlight, AnUnfinishedTaskIsInFlight) {
  std::array<WorkerTally, 2> workers;
  setTally(workers[0], 5, 5);
  setTally(workers[1], 8, 7);
  EXPECT_TRUE(anyTaskInFlight(workers.data(), workers.size()));

  setTally(workers[0], 0, 0);
  setTally(workers[1], 0, std::numeric_limits<std::size_t>::max());
  EXPECT_TRUE(anyTaskInFlight(workers.data(), workers.size()));
}

/** @test A task is in flight throughout while the work hands over from the first worker to the
 * second between the two sweeps: the second starts, then the first finishes. The answer is "in
 * flight". Reading the started counts before the finished ones would see one start and one
 * finish and answer "idle". */
TEST(PoolBaseInFlight, HandoverToALaterWorkerBetweenTheSweepsIsInFlight) {
  Script script;
  auto workers = twoWorkers(script, 1, 0, 0, 0);
  script.after = workers.size();
  script.step = [&workers] {
    workers[1].started.value = 1;
    workers[0].finished.value = 1;
  };

  EXPECT_TRUE(anyTaskInFlight(workers.data(), workers.size()));
  EXPECT_EQ(script.loads, 2 * workers.size());
}

/** @test The same handover in the other direction, from the second worker to the first, after
 * two counts have been read. Reading each worker's two counts together would see the first
 * worker idle, then the second with its task finished, and answer "idle". */
TEST(PoolBaseInFlight, HandoverToAnEarlierWorkerBetweenTheSweepsIsInFlight) {
  Script script;
  auto workers = twoWorkers(script, 0, 0, 1, 0);
  script.after = workers.size();
  script.step = [&workers] {
    workers[0].started.value = 1;
    workers[1].finished.value = 1;
  };

  EXPECT_TRUE(anyTaskInFlight(workers.data(), workers.size()));
  EXPECT_EQ(script.loads, 2 * workers.size());
}

/* ----------------------------- Worker Tally Tests ----------------------------- */

/** @test Every worker is started with a tally of its own: the tallies are written without a
 * locked instruction, which holds only while each has one writer. */
TEST(PoolBaseTally, EveryWorkerIsStartedWithItsOwnTally) {
  constexpr std::size_t WORKERS = 4;
  const TallyRecorder POOL(WORKERS);
  ASSERT_TRUE(POOL.allStarted());

  std::vector<const WorkerTally*> seen = POOL.seen();
  ASSERT_EQ(seen.size(), WORKERS);
  std::sort(seen.begin(), seen.end());
  EXPECT_EQ(std::adjacent_find(seen.begin(), seen.end()), seen.end())
      << "two workers were started with the same tally";
  EXPECT_FALSE(POOL.threadsRunning());
}
