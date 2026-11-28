/**
 * @file SchedulableComponentBase_uTest.cpp
 * @brief Unit tests for the schedulable base's task tables across reset and
 *        for the hardware model's transport link.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/HwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/TransportLink.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>

#include <unistd.h>

using system_core::system_component::HwModelBase;
using system_core::system_component::provisionTransport;
using system_core::system_component::SwModelBase;
using system_core::system_component::TransportKind;
using system_core::system_component::TransportLink;

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

  /// What the last doReset() found in the tables.
  struct SeenByReset {
    std::size_t tasks{0};
    std::size_t groups{0};
    bool plainTask{false};
    bool sequencedTask{false};
    bool group{false};
  };
  SeenByReset seenByReset;

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    registered = createSequenceGroup(0, 1) &&
                 registerTask<TwoTaskModel, &TwoTaskModel::plain>(PLAIN_UID, this, "plain") &&
                 registerSequencedTask<TwoTaskModel, &TwoTaskModel::sequenced>(SEQUENCED_UID, this,
                                                                               "sequenced", 0, 1);
    return registered ? 0 : 1;
  }

  void doReset() noexcept override {
    seenByReset.tasks = taskCount();
    seenByReset.groups = sequenceGroupCount();
    seenByReset.plainTask = taskByUid(PLAIN_UID) != nullptr;
    seenByReset.sequencedTask = taskByUid(SEQUENCED_UID) != nullptr;
    seenByReset.group = sequenceGroup(0) != nullptr;
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

/** @test The component's doReset() runs while its tasks and group are still in the tables. */
TEST(SchedulableComponentBaseTest, DoResetRunsBeforeTheTablesAreEmptied) {
  TwoTaskModel model;
  ASSERT_EQ(model.init(), 0u);

  model.reset();
  EXPECT_EQ(model.seenByReset.tasks, 2u);
  EXPECT_EQ(model.seenByReset.groups, 1u);
  EXPECT_TRUE(model.seenByReset.plainTask);
  EXPECT_TRUE(model.seenByReset.sequencedTask);
  EXPECT_TRUE(model.seenByReset.group);
  EXPECT_EQ(model.taskCount(), 0u);
  EXPECT_EQ(model.sequenceGroupCount(), 0u);
}

/* ----------------------------- Transport ----------------------------- */

namespace {

/// A software model that reports the hardware model type without being one.
class MislabelledModel final : public SwModelBase {
public:
  [[nodiscard]] std::uint16_t componentId() const noexcept override { return 994; }
  [[nodiscard]] const char* componentName() const noexcept override { return "Mislabelled"; }
  [[nodiscard]] const char* label() const noexcept override { return "MISLABELLED"; }
  [[nodiscard]] system_core::system_component::ComponentType
  componentType() const noexcept override {
    return system_core::system_component::ComponentType::HW_MODEL;
  }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override { return 0; }
};

} // namespace

/**
 * @test asHwModel() is the object itself for a hardware model and nullptr for
 *       every other component, whatever type the component reports.
 */
TEST(HwModelBaseTest, AsHwModelFollowsTheClassNotTheReportedType) {
  PairedModel hardware;
  TwoTaskModel software;
  MislabelledModel mislabelled;

  EXPECT_EQ(hardware.asHwModel(), &hardware);
  EXPECT_EQ(software.asHwModel(), nullptr);
  ASSERT_EQ(mislabelled.componentType(), system_core::system_component::ComponentType::HW_MODEL);
  EXPECT_EQ(mislabelled.asHwModel(), nullptr);
}

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

/** @test Asking an open link for another kind is refused, and the link stays as it was. */
TEST(TransportLinkTest, AnotherKindOnAnOpenLinkIsRefused) {
  const int BEFORE = openDescriptorCount();
  TransportLink link{};
  ASSERT_TRUE(provisionTransport(TransportKind::UNIX_STREAM, link));
  const int MODEL_FD = link.modelFd;
  const int PEER_FD = link.peerFd;
  ASSERT_EQ(openDescriptorCount(), BEFORE + 2);

  EXPECT_FALSE(provisionTransport(TransportKind::CAN, link));
  EXPECT_EQ(link.kind, TransportKind::UNIX_STREAM);
  EXPECT_EQ(link.modelFd, MODEL_FD);
  EXPECT_EQ(link.peerFd, PEER_FD);
  EXPECT_EQ(openDescriptorCount(), BEFORE + 2);

  const char OUT = 'y';
  ASSERT_EQ(::write(PEER_FD, &OUT, 1), 1);
  char in = 0;
  ASSERT_EQ(::read(MODEL_FD, &in, 1), 1);
  EXPECT_EQ(in, 'y');

  link.close();
  EXPECT_EQ(openDescriptorCount(), BEFORE);
}

/** @test A kind with no provisioner fails and leaves the caller's link untouched. */
TEST(TransportLinkTest, UnsupportedKindLeavesTheLinkUntouched) {
  const int BEFORE = openDescriptorCount();
  TransportLink link{};
  link.kind = TransportKind::SERIAL_232;
  std::strncpy(link.peerPath, "untouched", sizeof(link.peerPath) - 1);

  EXPECT_FALSE(provisionTransport(TransportKind::ETH_TCP, link));
  EXPECT_FALSE(link.isValid());
  EXPECT_EQ(link.peerFd, -1);
  EXPECT_EQ(link.kind, TransportKind::SERIAL_232);
  EXPECT_STREQ(link.peerPath, "untouched");
  EXPECT_EQ(openDescriptorCount(), BEFORE);
}
