/**
 * @file SchedulableComponentBase_uTest.cpp
 * @brief Unit tests for the schedulable base's task tables across reset and
 *        for the hardware model's transport link.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/HwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>

#include <unistd.h>

using system_core::system_component::HwModelBase;
using system_core::system_component::SwModelBase;
using system_core::system_component::TransportKind;

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

/// A hardware model whose transport is a socket pair.
class PairedModel final : public HwModelBase {
public:
  [[nodiscard]] std::uint16_t componentId() const noexcept override { return 995; }
  [[nodiscard]] const char* componentName() const noexcept override { return "PairedModel"; }
  [[nodiscard]] const char* label() const noexcept override { return "PAIRED"; }
  [[nodiscard]] TransportKind transportKind() const noexcept override {
    return TransportKind::UNIX_STREAM;
  }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override { return 0; }
};

/// Open descriptors of this process (the entries of /proc/self/fd).
int openDescriptorCount() {
  int count = 0;
  for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) {
    ++count;
  }
  return count;
}

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

/* ----------------------------- Transport ----------------------------- */

/** @test A second provisionTransport() keeps the open link: no descriptor is opened or dropped. */
TEST(HwModelBaseTest, ProvisioningTwiceKeepsTheLink) {
  const int BEFORE = openDescriptorCount();
  {
    PairedModel model;
    ASSERT_TRUE(model.provisionTransport());
    ASSERT_TRUE(model.hasTransport());
    const int MODEL_FD = model.transportFd();
    const int PEER_FD = model.peerFd();
    ASSERT_GE(PEER_FD, 0);
    const int PROVISIONED = openDescriptorCount();
    EXPECT_EQ(PROVISIONED, BEFORE + 2);

    EXPECT_TRUE(model.provisionTransport());
    EXPECT_EQ(openDescriptorCount(), PROVISIONED);
    EXPECT_EQ(model.transportFd(), MODEL_FD);
    EXPECT_EQ(model.peerFd(), PEER_FD);

    // The pair a driver would hold still carries bytes.
    const char OUT = 'x';
    ASSERT_EQ(::write(PEER_FD, &OUT, 1), 1);
    char in = 0;
    ASSERT_EQ(::read(MODEL_FD, &in, 1), 1);
    EXPECT_EQ(in, 'x');
  }
  EXPECT_EQ(openDescriptorCount(), BEFORE);
}
