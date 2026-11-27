/**
 * @file SchedulerTableGuard_uTest.cpp
 * @brief Reads of the task table from a thread that does not own it.
 *
 * The thread that dispatches also changes the table: it adds tasks, loads
 * a table and rebinds a component's tasks to a replacement. The clock
 * thread reads the table for its frame-loss line through
 * inFlightSummary(). A read holds a TableRead; a change waits for the
 * reads already begun and turns later ones away until it is done.
 *
 * Coverage:
 *  - A task added while a read is held is not added until the read ends.
 *  - So with a component's tasks rebound to a replacement, which rewrites
 *    the name each entry points to.
 *  - A read that arrives during a change goes without: TableRead tests
 *    false and inFlightSummary() says the table is changing.
 *  - inFlightSummary() called without pause from another thread while the
 *    table grows through many reallocations reads no freed entry.
 */

#include "src/system/core/components/scheduler/posix/inc/SchedulerMultiThread.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using apex::concurrency::DelegateU8;
using system_core::schedulable::SchedulableTask;
using system_core::scheduler::SchedulerBase;
using system_core::scheduler::SchedulerMultiThread;
using system_core::scheduler::Status;
using system_core::scheduler::TaskConfig;
using system_core::system_component::SwModelBase;

namespace {

constexpr std::uint32_t MODEL_UID = 0x00C900;
constexpr std::uint8_t STEP_UID = 1;

std::filesystem::path testLogDir() {
  const auto DIR = std::filesystem::temp_directory_path() /
                   ("scheduler_table_guard_utest_" + std::to_string(::getpid()));
  std::filesystem::create_directories(DIR);
  return DIR;
}

std::uint8_t noop(void*) noexcept { return 0; }

/// A scheduler that lets a test hold a table change open.
class GuardedScheduler final : public SchedulerMultiThread {
public:
  GuardedScheduler() : SchedulerMultiThread(100, testLogDir()) {}
  using SchedulerBase::TableChange;
};

/// A model with one task.
class OneTaskModel final : public SwModelBase {
public:
  explicit OneTaskModel(const char* name) noexcept : name_(name) {}
  [[nodiscard]] std::uint16_t componentId() const noexcept override { return 201; }
  [[nodiscard]] const char* componentName() const noexcept override { return name_.c_str(); }
  [[nodiscard]] const char* label() const noexcept override { return "ONE_TASK"; }
  [[nodiscard]] std::uint8_t step() noexcept { return 0; }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    return registerTask<OneTaskModel, &OneTaskModel::step>(STEP_UID, this, "step") ? 0 : 1;
  }

private:
  std::string name_;
};

/// A thread that takes a TableRead, says so, and holds it until released.
class HeldRead {
public:
  explicit HeldRead(const SchedulerBase& sched) {
    thread_ = std::thread([this, &sched]() {
      const SchedulerBase::TableRead READ(sched);
      taken_.store(static_cast<bool>(READ) ? 1 : 2, std::memory_order_release);
      while (!release_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::microseconds(200));
      }
    });
    while (taken_.load(std::memory_order_acquire) == 0) {
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
  }
  ~HeldRead() { end(); }
  HeldRead(const HeldRead&) = delete;
  HeldRead& operator=(const HeldRead&) = delete;

  /// True when the read was granted.
  [[nodiscard]] bool granted() const noexcept { return taken_.load() == 1; }

  void end() {
    release_.store(true, std::memory_order_release);
    if (thread_.joinable()) {
      thread_.join();
    }
  }

private:
  std::thread thread_;
  std::atomic<int> taken_{0};
  std::atomic<bool> release_{false};
};

/// Run CHANGE on a thread of its own; the test asks whether it has returned.
class Changer {
public:
  template <typename Fn> explicit Changer(Fn change) {
    thread_ = std::thread([this, change]() {
      change();
      done_.store(true, std::memory_order_release);
    });
  }
  ~Changer() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }
  Changer(const Changer&) = delete;
  Changer& operator=(const Changer&) = delete;

  [[nodiscard]] bool done() const noexcept { return done_.load(std::memory_order_acquire); }

  /// Whether the change returns within LIMIT.
  [[nodiscard]] bool doneWithin(std::chrono::milliseconds limit) const {
    const auto END = std::chrono::steady_clock::now() + limit;
    while (!done() && std::chrono::steady_clock::now() < END) {
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return done();
  }

private:
  std::thread thread_;
  std::atomic<bool> done_{false};
};

constexpr std::chrono::milliseconds HELD_FOR{50};
constexpr std::chrono::milliseconds LIMIT{5000};

} // namespace

/* ----------------------------- A change waits for a read ----------------------------- */

/** @test A task is not added while a read of the table is held. */
TEST(SchedulerTableGuardTest, AnAddedTaskWaitsForAReadInProgress) {
  GuardedScheduler sched;
  ASSERT_EQ(sched.init(), 0U);
  SchedulableTask first(DelegateU8{&noop, nullptr}, "first");
  SchedulableTask second(DelegateU8{&noop, nullptr}, "second");
  ASSERT_EQ(sched.addTask(first, TaskConfig(100, 1, 0)), Status::SUCCESS);

  HeldRead read(sched);
  ASSERT_TRUE(read.granted());
  const Changer ADD([&sched, &second]() { (void)sched.addTask(second, TaskConfig(100, 1, 0)); });

  EXPECT_FALSE(ADD.doneWithin(HELD_FOR)) << "the table grew under a reader";
  EXPECT_EQ(sched.entries().size(), 1U);

  read.end();
  EXPECT_TRUE(ADD.doneWithin(LIMIT));
  EXPECT_EQ(sched.entries().size(), 2U);
  sched.shutdown();
}

/** @test A component's tasks are not rebound while a read of the table is held. */
TEST(SchedulerTableGuardTest, ARebindingWaitsForAReadInProgress) {
  GuardedScheduler sched;
  ASSERT_EQ(sched.init(), 0U);
  OneTaskModel original("Original");
  OneTaskModel replacement("Replacement");
  ASSERT_EQ(original.init(), 0U);
  ASSERT_EQ(replacement.init(), 0U);
  ASSERT_EQ(sched.addTask(*original.taskByUid(STEP_UID), TaskConfig(100, 1, 0), nullptr, MODEL_UID,
                          STEP_UID, original.componentName()),
            Status::SUCCESS);

  HeldRead read(sched);
  ASSERT_TRUE(read.granted());
  const Changer REBIND(
      [&sched, &replacement]() { (void)sched.replaceComponentTasks(MODEL_UID, replacement); });

  EXPECT_FALSE(REBIND.doneWithin(HELD_FOR)) << "an entry's name was rewritten under a reader";
  EXPECT_STREQ(sched.entries().front().componentName, "Original");

  read.end();
  EXPECT_TRUE(REBIND.doneWithin(LIMIT));
  EXPECT_STREQ(sched.entries().front().componentName, "Replacement");
  sched.shutdown();
}

/* ----------------------------- A read during a change ----------------------------- */

/** @test A read that arrives during a change goes without and does not wait. */
TEST(SchedulerTableGuardTest, AReadDuringAChangeGoesWithout) {
  GuardedScheduler sched;
  ASSERT_EQ(sched.init(), 0U);
  ASSERT_EQ(sched.inFlightSummary(4), "none");

  {
    const GuardedScheduler::TableChange CHANGE(sched);
    HeldRead read(sched);
    EXPECT_FALSE(read.granted());
    read.end();

    std::string summary;
    const Changer DIAGNOSTIC([&sched, &summary]() { summary = sched.inFlightSummary(4); });
    ASSERT_TRUE(DIAGNOSTIC.doneWithin(LIMIT)) << "the diagnostic waited for the change";
    EXPECT_EQ(summary, SchedulerBase::TABLE_CHANGING_TEXT);
  }

  EXPECT_EQ(sched.inFlightSummary(4), "none") << "reads are granted again after the change";
  sched.shutdown();
}

/* ----------------------------- The diagnostic against a growing table
 * ----------------------------- */

/**
 * @test inFlightSummary() runs without pause on one thread while another
 *       adds tasks, growing the table through many reallocations. Every
 *       call returns one of its three texts; under a sanitizer a read of
 *       a moved entry would be reported.
 */
TEST(SchedulerTableGuardTest, TheDiagnosticReadsNoFreedEntryWhileTheTableGrows) {
  constexpr std::size_t TASKS = 2000;
  GuardedScheduler sched;
  ASSERT_EQ(sched.init(), 0U);
  std::vector<std::unique_ptr<SchedulableTask>> tasks;
  tasks.reserve(TASKS);
  for (std::size_t i = 0; i < TASKS; ++i) {
    tasks.push_back(std::make_unique<SchedulableTask>(DelegateU8{&noop, nullptr}, "grow"));
  }

  std::atomic<bool> stop{false};
  std::atomic<std::size_t> reads{0};
  std::atomic<std::size_t> unexpected{0};
  std::thread reader([&]() {
    while (!stop.load(std::memory_order_acquire)) {
      const std::string TEXT = sched.inFlightSummary(4);
      if (TEXT != "none" && TEXT != SchedulerBase::TABLE_CHANGING_TEXT) {
        unexpected.fetch_add(1, std::memory_order_relaxed);
      }
      reads.fetch_add(1, std::memory_order_relaxed);
    }
  });

  for (std::size_t i = 0; i < TASKS; ++i) {
    // One task a second on its own tick: the table grows, the slots stay small.
    ASSERT_EQ(sched.addTask(*tasks[i], TaskConfig(1, 1, static_cast<std::uint16_t>(i % 100))),
              Status::SUCCESS);
  }
  stop.store(true, std::memory_order_release);
  reader.join();

  EXPECT_EQ(sched.entries().size(), TASKS);
  EXPECT_GT(reads.load(), 0U);
  EXPECT_EQ(unexpected.load(), 0U);
  sched.shutdown();
}
