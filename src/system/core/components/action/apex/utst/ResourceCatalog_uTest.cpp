/**
 * @file ResourceCatalog_uTest.cpp
 * @brief Unit tests for WatchpointCatalog, GroupCatalog, NotificationCatalog.
 */

#include "src/system/core/components/action/apex/inc/ResourceCatalog.hpp"

#include <gtest/gtest.h>

#include <cstring>

using system_core::data::activateGroup;
using system_core::data::activateNotification;
using system_core::data::activateWatchpoint;
using system_core::data::DataCategory;
using system_core::data::DataWatchpoint;
using system_core::data::deactivateGroup;
using system_core::data::deactivateNotification;
using system_core::data::deactivateWatchpoint;
using system_core::data::EventNotification;
using system_core::data::GroupCatalog;
using system_core::data::GroupDef;
using system_core::data::GroupLogic;
using system_core::data::LogSeverity;
using system_core::data::NotificationCatalog;
using system_core::data::NotificationDef;
using system_core::data::WatchDataType;
using system_core::data::WATCHPOINT_GROUP_TABLE_SIZE;
using system_core::data::WATCHPOINT_TABLE_SIZE;
using system_core::data::WatchpointCatalog;
using system_core::data::WatchpointDef;
using system_core::data::WatchpointGroup;
using system_core::data::WatchPredicate;

/* ----------------------------- WatchpointCatalog ----------------------------- */

/** @test WatchpointCatalog starts empty. */
TEST(ResourceCatalog, WatchpointCatalogEmpty) {
  WatchpointCatalog cat;
  EXPECT_EQ(cat.size(), 0U);
  EXPECT_EQ(cat.findById(1), nullptr);
}

/** @test Add and find watchpoint definitions. */
TEST(ResourceCatalog, WatchpointCatalogAddFind) {
  WatchpointCatalog cat;

  WatchpointDef def{};
  def.watchpointId = 10;
  def.target = {0x007800, DataCategory::OUTPUT, 0, 4};
  def.predicate = WatchPredicate::GT;
  def.dataType = WatchDataType::FLOAT32;
  def.eventId = 1;
  float threshold = 50.0F;
  std::memcpy(def.threshold.data(), &threshold, 4);
  def.activeOnBoot = true;

  EXPECT_TRUE(cat.add(def));
  EXPECT_EQ(cat.size(), 1U);

  const auto* found = cat.findById(10);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->watchpointId, 10U);
  EXPECT_EQ(found->eventId, 1U);
  EXPECT_EQ(found->predicate, WatchPredicate::GT);
  EXPECT_TRUE(found->activeOnBoot);
}

/** @test Duplicate watchpoint ID rejected. */
TEST(ResourceCatalog, WatchpointCatalogDuplicate) {
  WatchpointCatalog cat;

  WatchpointDef def{};
  def.watchpointId = 5;
  EXPECT_TRUE(cat.add(def));
  EXPECT_FALSE(cat.add(def));
  EXPECT_EQ(cat.size(), 1U);
}

/** @test Sorted binary search with multiple entries. */
TEST(ResourceCatalog, WatchpointCatalogSortedLookup) {
  WatchpointCatalog cat;

  for (std::uint16_t id : {50, 10, 30, 20, 40}) {
    WatchpointDef def{};
    def.watchpointId = id;
    def.eventId = id;
    cat.add(def);
  }

  EXPECT_EQ(cat.size(), 5U);
  for (std::uint16_t id : {10, 20, 30, 40, 50}) {
    const auto* found = cat.findById(id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->eventId, id);
  }
  EXPECT_EQ(cat.findById(99), nullptr);
}

/* ----------------------------- GroupCatalog ----------------------------- */

/** @test GroupCatalog add and find. */
TEST(ResourceCatalog, GroupCatalogAddFind) {
  GroupCatalog cat;

  GroupDef def{};
  def.groupId = 5;
  def.refs = {1, 2, 0, 0};
  def.count = 2;
  def.logic = GroupLogic::AND;
  def.eventId = 10;
  def.activeOnBoot = true;

  EXPECT_TRUE(cat.add(def));

  const auto* found = cat.findById(5);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->count, 2U);
  EXPECT_EQ(found->refs[0], 1U);
  EXPECT_EQ(found->refs[1], 2U);
  EXPECT_EQ(found->logic, GroupLogic::AND);
}

/* ----------------------------- NotificationCatalog ----------------------------- */

/** @test NotificationCatalog add and find. */
TEST(ResourceCatalog, NotificationCatalogAddFind) {
  NotificationCatalog cat;

  NotificationDef def{};
  def.notificationId = 7;
  def.eventId = 3;
  def.severity = LogSeverity::WARNING;
  std::strncpy(def.logLabel, "TEST", sizeof(def.logLabel) - 1);
  std::strncpy(def.logMessage, "Threshold exceeded", sizeof(def.logMessage) - 1);
  def.activeOnBoot = true;

  EXPECT_TRUE(cat.add(def));

  const auto* found = cat.findById(7);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->eventId, 3U);
  EXPECT_EQ(found->severity, LogSeverity::WARNING);
  EXPECT_STREQ(found->logLabel, "TEST");
}

/* ----------------------------- Activate Watchpoint ----------------------------- */

/** @test Activate watchpoint from catalog into active table. */
TEST(ResourceCatalog, ActivateWatchpoint) {
  DataWatchpoint table[4]{};

  WatchpointDef def{};
  def.watchpointId = 42;
  def.target = {0x007800, DataCategory::OUTPUT, 0, 4};
  def.predicate = WatchPredicate::GT;
  def.dataType = WatchDataType::FLOAT32;
  def.eventId = 1;
  def.cadenceTicks = 10;

  const std::uint8_t SLOT = activateWatchpoint(def, table, 4);
  ASSERT_NE(SLOT, 0xFF);
  EXPECT_EQ(table[SLOT].watchpointId, 42U);
  EXPECT_EQ(table[SLOT].eventId, 1U);
  EXPECT_EQ(table[SLOT].predicate, WatchPredicate::GT);
  EXPECT_EQ(table[SLOT].cadenceTicks, 10U);
  EXPECT_TRUE(table[SLOT].armed);
}

/** @test Activate rejects duplicate. */
TEST(ResourceCatalog, ActivateWatchpointDuplicate) {
  DataWatchpoint table[4]{};

  WatchpointDef def{};
  def.watchpointId = 42;

  EXPECT_NE(activateWatchpoint(def, table, 4), 0xFF);
  EXPECT_EQ(activateWatchpoint(def, table, 4), 0xFF);
}

/** @test Activate returns 0xFF when table full. */
TEST(ResourceCatalog, ActivateWatchpointFull) {
  DataWatchpoint table[2]{};

  WatchpointDef d1{};
  d1.watchpointId = 1;
  WatchpointDef d2{};
  d2.watchpointId = 2;
  WatchpointDef d3{};
  d3.watchpointId = 3;

  EXPECT_NE(activateWatchpoint(d1, table, 2), 0xFF);
  EXPECT_NE(activateWatchpoint(d2, table, 2), 0xFF);
  EXPECT_EQ(activateWatchpoint(d3, table, 2), 0xFF);
}

/* ----------------------------- Deactivate Watchpoint ----------------------------- */

/** @test Deactivate removes watchpoint from active table. */
TEST(ResourceCatalog, DeactivateWatchpoint) {
  DataWatchpoint table[4]{};

  WatchpointDef def{};
  def.watchpointId = 42;
  def.eventId = 1;
  activateWatchpoint(def, table, 4);

  EXPECT_TRUE(deactivateWatchpoint(42, table, 4));
  EXPECT_EQ(table[0].watchpointId, 0U);
  EXPECT_FALSE(table[0].armed);
}

/** @test Deactivate returns false for non-existent ID. */
TEST(ResourceCatalog, DeactivateWatchpointNotFound) {
  DataWatchpoint table[4]{};
  EXPECT_FALSE(deactivateWatchpoint(99, table, 4));
}

/** @test Reactivate after deactivate works. */
TEST(ResourceCatalog, ReactivateWatchpoint) {
  DataWatchpoint table[4]{};

  WatchpointDef def{};
  def.watchpointId = 42;
  def.eventId = 1;

  EXPECT_NE(activateWatchpoint(def, table, 4), 0xFF);
  EXPECT_TRUE(deactivateWatchpoint(42, table, 4));
  EXPECT_NE(activateWatchpoint(def, table, 4), 0xFF);
}

/* ----------------------------- Activate/Deactivate Group ----------------------------- */

/** @test Activate group from catalog into active table. */
TEST(ResourceCatalog, ActivateGroup) {
  WatchpointGroup table[4]{};

  GroupDef def{};
  def.groupId = 10;
  def.refs = {1, 2, 0, 0};
  def.count = 2;
  def.logic = GroupLogic::OR;
  def.eventId = 5;

  const std::uint8_t SLOT = activateGroup(def, table, 4);
  ASSERT_NE(SLOT, 0xFF);
  EXPECT_EQ(table[SLOT].groupId, 10U);
  EXPECT_EQ(table[SLOT].refs[0], 1U);
  EXPECT_EQ(table[SLOT].refs[1], 2U);
  EXPECT_EQ(table[SLOT].logic, GroupLogic::OR);
  EXPECT_TRUE(table[SLOT].armed);
}

/** @test Deactivate group. */
TEST(ResourceCatalog, DeactivateGroup) {
  WatchpointGroup table[4]{};

  GroupDef def{};
  def.groupId = 10;
  activateGroup(def, table, 4);

  EXPECT_TRUE(deactivateGroup(10, table, 4));
  EXPECT_EQ(table[0].groupId, 0U);
}

/* ----------------------------- Activate/Deactivate Notification ----------------------------- */

/** @test Activate notification from catalog into active table. */
TEST(ResourceCatalog, ActivateNotification) {
  EventNotification table[4]{};

  NotificationDef def{};
  def.notificationId = 20;
  def.eventId = 3;
  def.severity = LogSeverity::WARNING;
  std::strncpy(def.logLabel, "ALERT", sizeof(def.logLabel) - 1);
  std::strncpy(def.logMessage, "Temp high", sizeof(def.logMessage) - 1);

  const std::uint8_t SLOT = activateNotification(def, table, 4);
  ASSERT_NE(SLOT, 0xFF);
  EXPECT_EQ(table[SLOT].notificationId, 20U);
  EXPECT_EQ(table[SLOT].eventId, 3U);
  EXPECT_STREQ(table[SLOT].logLabel, "ALERT");
  EXPECT_TRUE(table[SLOT].armed);
}

/** @test Deactivate notification. */
TEST(ResourceCatalog, DeactivateNotification) {
  EventNotification table[4]{};

  NotificationDef def{};
  def.notificationId = 20;
  activateNotification(def, table, 4);

  EXPECT_TRUE(deactivateNotification(20, table, 4));
  EXPECT_EQ(table[0].notificationId, 0U);
}

/* ----------------------------- ForEach ----------------------------- */

/** @test ForEach iterates all catalog entries. */
TEST(ResourceCatalog, ForEachIterates) {
  WatchpointCatalog cat;

  for (std::uint16_t i = 1; i <= 5; ++i) {
    WatchpointDef def{};
    def.watchpointId = i;
    cat.add(def);
  }

  std::uint16_t sum = 0;
  cat.forEach([&](const WatchpointDef& e) { sum += e.watchpointId; });
  EXPECT_EQ(sum, 15U);
}

/** @test Clear empties catalog. */
TEST(ResourceCatalog, ClearCatalog) {
  WatchpointCatalog cat;

  WatchpointDef def{};
  def.watchpointId = 1;
  cat.add(def);
  EXPECT_EQ(cat.size(), 1U);

  cat.clear();
  EXPECT_EQ(cat.size(), 0U);
  EXPECT_EQ(cat.findById(1), nullptr);
}
