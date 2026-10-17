/**
 * @file ActionComponent_uTest.cpp
 * @brief Unit tests for ActionComponent lifecycle and delegation.
 */

#include "src/system/core/components/action/apex/inc/ActionComponent.hpp"
#include "src/system/core/components/action/apex/inc/ActionComponentStatus.hpp"
#include "src/system/core/components/action/apex/inc/ActionEngineTprm.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/CommandResult.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/TprmPayload.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <unistd.h>

/* ----------------------------- Test Helpers ----------------------------- */

namespace {

struct TestBlock {
  std::array<std::uint8_t, 64> bytes{};
};

struct ResolverCtx {
  TestBlock* block{nullptr};
  std::uint32_t lastUid{0};
};

static system_core::data::ResolvedData
testResolver(void* ctx, std::uint32_t uid, system_core::data::DataCategory /*cat*/) noexcept {
  auto* r = static_cast<ResolverCtx*>(ctx);
  r->lastUid = uid;
  if (r->block == nullptr) {
    return {};
  }
  return {r->block->bytes.data(), r->block->bytes.size()};
}

struct CommandCtx {
  std::uint32_t lastUid{0};
  std::uint16_t lastOpcode{0};
  std::uint8_t callCount{0};
};

static void testCmdHandler(void* ctx, std::uint32_t uid, std::uint16_t opcode,
                           const std::uint8_t* /*payload*/, std::uint8_t /*len*/) noexcept {
  auto* c = static_cast<CommandCtx*>(ctx);
  c->lastUid = uid;
  c->lastOpcode = opcode;
  ++c->callCount;
}

} // namespace

/* ----------------------------- TPRM Load Contract ----------------------------- */

/** @test Missing tprm is optional: defaults, success, no executive warning. */
TEST(ActionComponent, LoadTprmMissingFileIsOptional) {
  const auto DIR =
      std::filesystem::temp_directory_path() / ("action_tprm_missing_" + std::to_string(getpid()));
  std::filesystem::create_directories(DIR);

  system_core::action::ActionComponent comp;
  comp.setInstanceIndex(0); // Registered component: the executive-boot precondition.
  EXPECT_EQ(comp.loadTprm(DIR), system_core::system_component::TprmIngest::DEFAULTS);
  EXPECT_TRUE(comp.paramsOptional());

  std::filesystem::remove_all(DIR);
}

/** @test Present-but-corrupt tprm still fails loudly. */
TEST(ActionComponent, LoadTprmCorruptFileFails) {
  const auto DIR =
      std::filesystem::temp_directory_path() / ("action_tprm_corrupt_" + std::to_string(getpid()));
  std::filesystem::create_directories(DIR);

  system_core::action::ActionComponent comp;
  comp.setInstanceIndex(0);
  char filename[32];
  std::snprintf(filename, sizeof(filename), "%06x.tprm", comp.fullUid());
  {
    std::ofstream f(DIR / filename, std::ios::binary);
    f << "not a tprm payload";
  }
  EXPECT_EQ(comp.loadTprm(DIR), system_core::system_component::TprmIngest::REJECTED);

  std::filesystem::remove_all(DIR);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Component identity returns correct ID and name. */
TEST(ActionComponent, ComponentIdentity) {
  system_core::action::ActionComponent comp;
  EXPECT_EQ(comp.componentId(), 5U);
  EXPECT_STREQ(comp.componentName(), "Action");
  EXPECT_STREQ(comp.label(), "ACTION");
}

/** @test Default construction yields configured but uninitialized state. */
TEST(ActionComponent, DefaultState) {
  system_core::action::ActionComponent comp;
  EXPECT_FALSE(comp.isInitialized());
  EXPECT_TRUE(comp.isConfigured());
  EXPECT_EQ(comp.stats().totalCycles, 0U);
}

/* ----------------------------- Init Tests ----------------------------- */

/** @test Init fails without resolver delegate. */
TEST(ActionComponent, InitFailsWithoutResolver) {
  system_core::action::ActionComponent comp;
  const std::uint8_t RESULT = comp.init();
  EXPECT_EQ(RESULT, static_cast<std::uint8_t>(system_core::action::Status::ERROR_NO_RESOLVER));
  EXPECT_FALSE(comp.isInitialized());
}

/** @test Init succeeds with resolver set. */
TEST(ActionComponent, InitSucceeds) {
  system_core::action::ActionComponent comp;
  ResolverCtx rCtx;
  comp.setResolver(testResolver, &rCtx);
  const std::uint8_t RESULT = comp.init();
  EXPECT_EQ(RESULT, static_cast<std::uint8_t>(system_core::action::Status::SUCCESS));
  EXPECT_TRUE(comp.isInitialized());
}

/** @test Init is idempotent. */
TEST(ActionComponent, InitIdempotent) {
  system_core::action::ActionComponent comp;
  ResolverCtx rCtx;
  comp.setResolver(testResolver, &rCtx);
  EXPECT_EQ(comp.init(), static_cast<std::uint8_t>(system_core::action::Status::SUCCESS));
  EXPECT_TRUE(comp.isInitialized());
  const std::uint8_t RESULT = comp.init();
  EXPECT_EQ(RESULT, static_cast<std::uint8_t>(system_core::action::Status::SUCCESS));
}

/* ----------------------------- Reset Tests ----------------------------- */

/** @test Reset clears tables and stats but preserves delegates. */
TEST(ActionComponent, ResetPreservesDelegates) {
  system_core::action::ActionComponent comp;
  ResolverCtx rCtx;
  TestBlock block;
  rCtx.block = &block;
  CommandCtx cCtx;
  comp.setResolver(testResolver, &rCtx);
  comp.setCommandHandler(testCmdHandler, &cCtx);
  EXPECT_EQ(comp.init(), static_cast<std::uint8_t>(system_core::action::Status::SUCCESS));

  // Run a cycle to bump stats
  comp.tick(0);
  EXPECT_EQ(comp.stats().totalCycles, 1U);

  // Reset
  comp.reset();
  EXPECT_FALSE(comp.isInitialized());
  EXPECT_EQ(comp.stats().totalCycles, 0U);

  // Resolver still works after reset (configured flag preserved by reset())
  const std::uint8_t RESULT = comp.init();
  EXPECT_EQ(RESULT, static_cast<std::uint8_t>(system_core::action::Status::SUCCESS));
}

/* ----------------------------- Tick Tests ----------------------------- */

/** @test Tick increments totalCycles. */
TEST(ActionComponent, TickIncrementsCycles) {
  system_core::action::ActionComponent comp;
  ResolverCtx rCtx;
  TestBlock block;
  rCtx.block = &block;
  comp.setResolver(testResolver, &rCtx);
  EXPECT_EQ(comp.init(), static_cast<std::uint8_t>(system_core::action::Status::SUCCESS));

  comp.tick(0);
  comp.tick(1);
  comp.tick(2);
  EXPECT_EQ(comp.stats().totalCycles, 3U);
}

/** @test Tick with armed watchpoint triggers event flow. */
TEST(ActionComponent, TickWithWatchpoint) {
  system_core::action::ActionComponent comp;
  TestBlock block;
  ResolverCtx rCtx{&block};
  CommandCtx cCtx;
  comp.setResolver(testResolver, &rCtx);
  comp.setCommandHandler(testCmdHandler, &cCtx);
  EXPECT_EQ(comp.init(), static_cast<std::uint8_t>(system_core::action::Status::SUCCESS));

  // Set up a watchpoint: byte[0] > 0
  auto& wp = comp.iface().watchpoints[0];
  wp.target = {0x007800, system_core::data::DataCategory::OUTPUT, 0, 1};
  wp.predicate = system_core::data::WatchPredicate::GT;
  wp.dataType = system_core::data::WatchDataType::UINT8;
  wp.eventId = 42;
  wp.threshold[0] = 0;
  wp.armed = true;

  // Set data to trigger: byte[0] = 5
  block.bytes[0] = 5;

  comp.tick(0);
  EXPECT_EQ(comp.stats().watchpointsFired, 1U);
}

/* ----------------------------- Catalog Activation Keeps Config ----------------------------- */

namespace {

/// A fresh directory only this process owns (mode 0700, name chosen atomically), or empty.
std::filesystem::path makePrivateDir(const char* tag) {
  std::string pattern =
      (std::filesystem::temp_directory_path() / (std::string(tag) + "_XXXXXX")).string();
  const char* MADE = ::mkdtemp(pattern.data());
  return (MADE == nullptr) ? std::filesystem::path{} : std::filesystem::path{MADE};
}

/// Write a CRC-bearing engine TPRM for `comp` into `dir`, as the loader expects it on disk.
/// Returns false if the file could not be written in full.
bool writeEngineTprm(const std::filesystem::path& dir, system_core::action::ActionComponent& comp,
                     const system_core::action::ActionEngineTprm& config) {
  namespace sc = system_core::system_component;
  sc::TprmPayloadHeader header{};
  header.magic = sc::TPRM_PAYLOAD_MAGIC;
  header.version = sc::TPRM_PAYLOAD_VERSION;
  header.fullUid = comp.fullUid();
  header.payloadSize = sizeof(config);
  header.payloadCrc = sc::tprmCrc32(reinterpret_cast<const std::uint8_t*>(&config), sizeof(config));
  char filename[32];
  std::snprintf(filename, sizeof(filename), "%06x.tprm", comp.fullUid());
  std::ofstream out(dir / filename, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(&header), sizeof(header));
  out.write(reinterpret_cast<const char*>(&config), sizeof(config));
  return out.good();
}

/// One armed watchpoint (byte[0] > 0) and two groups over it: group 1 armed at boot with a
/// debounce of three, group 2 left unarmed with a debounce of two.
system_core::action::ActionEngineTprm debouncedGroupsConfig() {
  system_core::action::ActionEngineTprm config{};
  auto& wp = config.watchpoints[0];
  wp.targetFullUid = 0x007800;
  wp.targetCategory = static_cast<std::uint8_t>(system_core::data::DataCategory::OUTPUT);
  wp.targetByteLen = 1;
  wp.predicate = static_cast<std::uint8_t>(system_core::data::WatchPredicate::GT);
  wp.dataType = static_cast<std::uint8_t>(system_core::data::WatchDataType::UINT8);
  wp.armed = 1;
  auto& boot = config.groups[0];
  boot.refCount = 1; // refs[0] = 0: watchpoint 1
  boot.eventId = 42;
  boot.minFireCount = 3;
  boot.armed = 1;
  auto& later = config.groups[1];
  later.refCount = 1;
  later.eventId = 43;
  later.minFireCount = 2;
  later.armed = 0;
  return config;
}

const system_core::data::WatchpointGroup* findArmedGroup(system_core::action::ActionComponent& comp,
                                                         std::uint16_t groupId) {
  for (const auto& group : comp.iface().groups) {
    if (group.armed && group.groupId == groupId) {
      return &group;
    }
  }
  return nullptr;
}

constexpr std::uint16_t OP_ACTIVATE_GROUP = 0x0532;
constexpr std::uint16_t OP_DEACTIVATE_GROUP = 0x0533;

} // namespace

/** @test A group's configured debounce survives DEACTIVATE_GROUP then ACTIVATE_GROUP. */
TEST(ActionComponent, GroupDebounceSurvivesReactivation) {
  namespace sc = system_core::system_component;
  const auto DIR = makePrivateDir("action_tprm_group_reactivate");
  ASSERT_FALSE(DIR.empty());

  system_core::action::ActionComponent comp;
  comp.setInstanceIndex(0);
  TestBlock block;
  block.bytes[0] = 5; // byte[0] > 0 holds on every tick
  ResolverCtx rCtx{&block};
  comp.setResolver(testResolver, &rCtx);
  ASSERT_TRUE(writeEngineTprm(DIR, comp, debouncedGroupsConfig()));
  ASSERT_EQ(comp.loadTprm(DIR), sc::TprmIngest::LOADED);
  ASSERT_EQ(comp.init(), static_cast<std::uint8_t>(system_core::action::Status::SUCCESS));

  // Boot: one dispatch, on the third held evaluation.
  comp.tick(0);
  comp.tick(1);
  EXPECT_EQ(comp.stats().groupsFired, 0U);
  comp.tick(2);
  EXPECT_EQ(comp.stats().groupsFired, 1U);
  comp.tick(3);
  EXPECT_EQ(comp.stats().groupsFired, 1U);

  std::uint8_t id[2] = {1, 0};
  std::vector<std::uint8_t> response;
  const auto OK = static_cast<std::uint8_t>(sc::CommandResult::SUCCESS);
  EXPECT_EQ(comp.handleCommand(OP_DEACTIVATE_GROUP, {id, 2}, response), OK);
  EXPECT_EQ(comp.handleCommand(OP_ACTIVATE_GROUP, {id, 2}, response), OK);
  const auto* group = findArmedGroup(comp, 1);
  ASSERT_NE(group, nullptr);
  EXPECT_EQ(group->minFireCount, 3U);
  EXPECT_EQ(group->heldCount, 0U);
  EXPECT_FALSE(group->latched);

  // The same configured delay applies again before the next single dispatch.
  comp.tick(4);
  comp.tick(5);
  EXPECT_EQ(comp.stats().groupsFired, 1U);
  comp.tick(6);
  EXPECT_EQ(comp.stats().groupsFired, 2U);
  comp.tick(7);
  EXPECT_EQ(comp.stats().groupsFired, 2U);

  std::filesystem::remove_all(DIR);
}

/** @test A group configured unarmed receives its debounce when first activated by command. */
TEST(ActionComponent, UnarmedGroupActivatesWithDebounce) {
  namespace sc = system_core::system_component;
  const auto DIR = makePrivateDir("action_tprm_group_activate");
  ASSERT_FALSE(DIR.empty());

  system_core::action::ActionComponent comp;
  comp.setInstanceIndex(0);
  TestBlock block;
  block.bytes[0] = 5;
  ResolverCtx rCtx{&block};
  comp.setResolver(testResolver, &rCtx);
  ASSERT_TRUE(writeEngineTprm(DIR, comp, debouncedGroupsConfig()));
  ASSERT_EQ(comp.loadTprm(DIR), sc::TprmIngest::LOADED);
  ASSERT_EQ(comp.init(), static_cast<std::uint8_t>(system_core::action::Status::SUCCESS));
  ASSERT_EQ(findArmedGroup(comp, 2), nullptr); // in the catalog only

  std::uint8_t id[2] = {2, 0};
  std::vector<std::uint8_t> response;
  EXPECT_EQ(comp.handleCommand(OP_ACTIVATE_GROUP, {id, 2}, response),
            static_cast<std::uint8_t>(sc::CommandResult::SUCCESS));
  const auto* group = findArmedGroup(comp, 2);
  ASSERT_NE(group, nullptr);
  EXPECT_EQ(group->minFireCount, 2U);
  EXPECT_EQ(group->eventId, 43U);

  // Group 2 dispatches on its second held evaluation; the boot group (three) follows one tick
  // later.
  comp.tick(0);
  EXPECT_EQ(comp.stats().groupsFired, 0U);
  comp.tick(1);
  EXPECT_EQ(comp.stats().groupsFired, 1U);
  comp.tick(2);
  EXPECT_EQ(comp.stats().groupsFired, 2U);
  comp.tick(3);
  EXPECT_EQ(comp.stats().groupsFired, 2U);

  std::filesystem::remove_all(DIR);
}

/* ----------------------------- Interface Access ----------------------------- */

/** @test Mutable iface() allows direct table configuration. */
TEST(ActionComponent, IfaceAccess) {
  system_core::action::ActionComponent comp;

  auto& iface = comp.iface();
  iface.watchpoints[0].armed = true;
  iface.watchpoints[0].eventId = 99;

  const auto& constIface = std::as_const(comp).iface();
  EXPECT_TRUE(constIface.watchpoints[0].armed);
  EXPECT_EQ(constIface.watchpoints[0].eventId, 99U);
}

/* ----------------------------- Status Tests ----------------------------- */

/** @test Status toString covers all values. */
TEST(ActionComponentStatus, ToStringCoversAll) {
  using system_core::action::Status;
  using system_core::action::toString;

  EXPECT_STREQ(toString(Status::SUCCESS), "SUCCESS");
  EXPECT_STREQ(toString(Status::ERROR_NO_RESOLVER), "ERROR_NO_RESOLVER");
  EXPECT_STREQ(toString(Status::ERROR_QUEUE_FULL), "ERROR_QUEUE_FULL");
  EXPECT_STREQ(toString(Status::WARN_RESOLVE_FAILURES), "WARN_RESOLVE_FAILURES");
  EXPECT_STREQ(toString(Status::EOE_ACTION), "EOE_ACTION");
}
