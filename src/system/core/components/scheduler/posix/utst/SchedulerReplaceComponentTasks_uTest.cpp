/**
 * @file SchedulerReplaceComponentTasks_uTest.cpp
 * @brief Binding a component's scheduler entries to a replacement instance.
 *
 * A hot-swap hands the scheduler a new instance of a component whose
 * tasks it has scheduled. Each entry of that component holds a task
 * pointer, for a sequenced task the counter of the task's sequence group,
 * and the component's name. All three belong to the instance, so all
 * three must move, and none may move unless every entry can.
 *
 * Coverage:
 *  - After a replacement every entry's task and sequence counter are the
 *    new instance's, and the entry's name is a copy the scheduler owns.
 *  - A replacement that lacks a scheduled task, or sequences one at
 *    another phase or not at all, changes nothing.
 *  - Nor does one that keeps every task's phase but puts the tasks of
 *    one sequence into two groups: each would wait on a counter its
 *    predecessor no longer advances.
 */

#include "src/system/core/components/scheduler/posix/inc/SchedulerMultiThread.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

using system_core::scheduler::SchedulerMultiThread;
using system_core::scheduler::Status;
using system_core::scheduler::TaskConfig;
using system_core::system_component::SwModelBase;

namespace {

constexpr std::uint32_t MODEL_UID = 0x00C800;
constexpr std::uint8_t PLAIN_UID = 1;
constexpr std::uint8_t FIRST_UID = 2;
constexpr std::uint8_t SECOND_UID = 3;
constexpr std::uint8_t EXTRA_A_UID = 4;
constexpr std::uint8_t EXTRA_B_UID = 5;

std::filesystem::path testLogDir() {
  const auto DIR = std::filesystem::temp_directory_path() /
                   ("scheduler_replace_utest_" + std::to_string(::getpid()));
  std::filesystem::create_directories(DIR);
  return DIR;
}

/// A model with one plain task and two tasks in sequence group 0.
class SequencedModel : public SwModelBase {
public:
  /// What an instance registers.
  enum class Shape : std::uint8_t {
    STANDARD,      ///< plain; first at phase 1; second at phase 2.
    NO_SECOND,     ///< as STANDARD without the second sequenced task.
    OTHER_PHASES,  ///< the two sequenced tasks at each other's phase.
    NOT_SEQUENCED, ///< all three tasks outside any group.
    /// first and second at their phases, each in a group of its own
    /// beside a task the schedule does not name.
    SPLIT_GROUPS
  };

  explicit SequencedModel(const char* name, Shape shape = Shape::STANDARD) noexcept
      : name_(name), shape_(shape) {}

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return 200; }
  [[nodiscard]] const char* componentName() const noexcept override { return name_.c_str(); }
  [[nodiscard]] const char* label() const noexcept override { return "SEQUENCED"; }

  [[nodiscard]] std::uint8_t noop() noexcept { return 0; }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    bool ok = registerTask<SequencedModel, &SequencedModel::noop>(PLAIN_UID, this, "plain");
    if (shape_ == Shape::NOT_SEQUENCED) {
      ok = ok && registerTask<SequencedModel, &SequencedModel::noop>(FIRST_UID, this, "first") &&
           registerTask<SequencedModel, &SequencedModel::noop>(SECOND_UID, this, "second");
      return ok ? 0 : 1;
    }
    if (shape_ == Shape::SPLIT_GROUPS) {
      // Group 0: first at 1, an extra at 2. Group 1: an extra at 1, second at 2.
      ok = ok && createSequenceGroup(0, 2) && createSequenceGroup(1, 2) &&
           registerSequencedTask<SequencedModel, &SequencedModel::noop>(FIRST_UID, this, "first", 0,
                                                                        1) &&
           registerSequencedTask<SequencedModel, &SequencedModel::noop>(EXTRA_A_UID, this,
                                                                        "extra_a", 0, 2) &&
           registerSequencedTask<SequencedModel, &SequencedModel::noop>(EXTRA_B_UID, this,
                                                                        "extra_b", 1, 1) &&
           registerSequencedTask<SequencedModel, &SequencedModel::noop>(SECOND_UID, this, "second",
                                                                        1, 2);
      return ok ? 0 : 1;
    }
    const int FIRST_PHASE = (shape_ == Shape::OTHER_PHASES) ? 2 : 1;
    const int SECOND_PHASE = (shape_ == Shape::OTHER_PHASES) ? 1 : 2;
    ok = ok && createSequenceGroup(0, 2) &&
         registerSequencedTask<SequencedModel, &SequencedModel::noop>(FIRST_UID, this, "first", 0,
                                                                      FIRST_PHASE);
    if (shape_ != Shape::NO_SECOND) {
      ok = ok && registerSequencedTask<SequencedModel, &SequencedModel::noop>(
                     SECOND_UID, this, "second", 0, SECOND_PHASE);
    }
    return ok ? 0 : 1;
  }

private:
  std::string name_; ///< Owned by the instance, as a plugin's name is by its library.
  Shape shape_;
};

/// A scheduler with MODEL's three tasks scheduled as a schedule file would.
std::unique_ptr<SchedulerMultiThread> scheduleAllOf(SequencedModel& model) {
  auto sched = std::make_unique<SchedulerMultiThread>(100, testLogDir());
  EXPECT_EQ(sched->init(), 0);
  EXPECT_EQ(sched->addTask(*model.taskByUid(PLAIN_UID), TaskConfig(100, 1, 0), nullptr, MODEL_UID,
                           PLAIN_UID, model.componentName()),
            Status::SUCCESS);
  for (const std::uint8_t UID : {FIRST_UID, SECOND_UID}) {
    EXPECT_EQ(sched->addTask(*model.taskByUid(UID), TaskConfig(100, 1, 0), model.sequenceGroup(0),
                             MODEL_UID, UID, model.componentName()),
              Status::SUCCESS);
  }
  return sched;
}

} // namespace

/** @test Every entry's task, sequence counter and name follow a replacement. */
TEST(SchedulerReplaceComponentTasksTest, EveryBindingFollowsTheReplacement) {
  SequencedModel original("Original");
  ASSERT_EQ(original.init(), 0u);
  auto sched = scheduleAllOf(original);
  ASSERT_EQ(sched->entries().size(), 3u);

  auto replacement = std::make_unique<SequencedModel>("Replacement");
  ASSERT_EQ(replacement->init(), 0u);
  EXPECT_EQ(sched->replaceComponentTasks(MODEL_UID, *replacement), 3u);

  for (const auto& entry : sched->entries()) {
    EXPECT_EQ(entry.task, replacement->taskByUid(entry.taskUid)) << int(entry.taskUid);
    EXPECT_STREQ(entry.componentName, "Replacement");
    EXPECT_NE(entry.componentName, replacement->componentName())
        << "the entry borrows the instance's name";
    if (entry.taskUid == PLAIN_UID) {
      EXPECT_EQ(entry.seqCounter, nullptr);
    } else {
      EXPECT_EQ(entry.seqCounter, replacement->sequenceGroup(0)->counter()) << int(entry.taskUid);
      EXPECT_NE(entry.seqCounter, original.sequenceGroup(0)->counter());
    }
  }

  // A second replacement, with the first one gone: nothing the scheduler
  // holds may still belong to it.
  auto second = std::make_unique<SequencedModel>("Second");
  ASSERT_EQ(second->init(), 0u);
  EXPECT_EQ(sched->replaceComponentTasks(MODEL_UID, *second), 3u);
  replacement.reset();
  for (const auto& entry : sched->entries()) {
    EXPECT_EQ(entry.task, second->taskByUid(entry.taskUid));
    EXPECT_STREQ(entry.componentName, "Second");
  }

  sched->shutdown();
}

/** @test A replacement that cannot take every entry over takes none. */
TEST(SchedulerReplaceComponentTasksTest, AnIncompleteReplacementChangesNothing) {
  SequencedModel original("Original");
  ASSERT_EQ(original.init(), 0u);
  auto sched = scheduleAllOf(original);

  for (const auto SHAPE :
       {SequencedModel::Shape::NO_SECOND, SequencedModel::Shape::OTHER_PHASES,
        SequencedModel::Shape::NOT_SEQUENCED, SequencedModel::Shape::SPLIT_GROUPS}) {
    SequencedModel refused("Refused", SHAPE);
    ASSERT_EQ(refused.init(), 0u);
    EXPECT_EQ(sched->replaceComponentTasks(MODEL_UID, refused), 0u) << int(SHAPE);

    for (const auto& entry : sched->entries()) {
      EXPECT_EQ(entry.task, original.taskByUid(entry.taskUid)) << int(SHAPE);
      EXPECT_STREQ(entry.componentName, "Original");
      if (entry.taskUid != PLAIN_UID) {
        EXPECT_EQ(entry.seqCounter, original.sequenceGroup(0)->counter()) << int(SHAPE);
      }
    }
  }

  // Another component's uid matches no entry.
  SequencedModel other("Other");
  ASSERT_EQ(other.init(), 0u);
  EXPECT_EQ(sched->replaceComponentTasks(MODEL_UID + 1U, other), 0u);

  sched->shutdown();
}
