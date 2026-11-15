/**
 * @file SchedulableComponentBase_uTest.cpp
 * @brief Unit tests for the schedulable base's task tables across reset.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"

#include <gtest/gtest.h>

#include <cstdint>

using system_core::system_component::SwModelBase;

namespace {

/// A model that registers one plain task and one sequenced task in a group.
class TwoTaskModel final : public SwModelBase {
public:
  static constexpr std::uint8_t PLAIN_UID = 1;
  static constexpr std::uint8_t SEQUENCED_UID = 2;

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return 996; }
  [[nodiscard]] const char* componentName() const noexcept override { return "TwoTaskModel"; }
  [[nodiscard]] const char* label() const noexcept override { return "TWO_TASK"; }

  std::uint8_t plain() noexcept { return 0; }
  std::uint8_t sequenced() noexcept { return 0; }

  bool registered{false};

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    registered = createSequenceGroup(0, 1) &&
                 registerTask<TwoTaskModel, &TwoTaskModel::plain>(PLAIN_UID, this, "plain") &&
                 registerSequencedTask<TwoTaskModel, &TwoTaskModel::sequenced>(SEQUENCED_UID, this,
                                                                               "sequenced", 0, 1);
    return registered ? 0 : 1;
  }
};

} // namespace

/* ----------------------------- Reset ----------------------------- */

/** @test reset() empties the task and group tables, so init() registers each task once. */
TEST(SchedulableComponentBaseTest, ResetThenInitRegistersEachTaskOnce) {
  TwoTaskModel model;
  ASSERT_EQ(model.init(), 0u);
  ASSERT_EQ(model.taskCount(), 2u);
  ASSERT_EQ(model.sequenceGroupCount(), 1u);

  model.reset();
  EXPECT_EQ(model.taskCount(), 0u);
  EXPECT_EQ(model.sequenceGroupCount(), 0u);
  EXPECT_EQ(model.taskByUid(TwoTaskModel::PLAIN_UID), nullptr);
  EXPECT_EQ(model.taskByUid(TwoTaskModel::SEQUENCED_UID), nullptr);
  EXPECT_EQ(model.sequenceGroup(0), nullptr);

  for (int cycle = 0; cycle < 3; ++cycle) {
    ASSERT_EQ(model.init(), 0u) << "cycle " << cycle;
    EXPECT_TRUE(model.registered);
    EXPECT_EQ(model.taskCount(), 2u) << "cycle " << cycle;
    EXPECT_EQ(model.sequenceGroupCount(), 1u);
    EXPECT_NE(model.taskByUid(TwoTaskModel::PLAIN_UID), nullptr);
    EXPECT_NE(model.taskByUid(TwoTaskModel::SEQUENCED_UID), nullptr);
    EXPECT_NE(model.sequenceGroup(0), nullptr);
    model.reset();
  }
}
