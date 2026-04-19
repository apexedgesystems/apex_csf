/**
 * @file DataWatchpoint_uTest.cpp
 * @brief Unit tests for DataWatchpoint, WatchpointGroup, and evaluation functions.
 */

#include "src/system/core/components/action/apex/inc/DataWatchpoint.hpp"

#include <gtest/gtest.h>

#include <cstring>

using system_core::data::applyWatchFunction;
using system_core::data::DataCategory;
using system_core::data::DataTarget;
using system_core::data::dataTypeSize;
using system_core::data::DataWatchpoint;
using system_core::data::evaluateEdge;
using system_core::data::evaluateGroup;
using system_core::data::evaluateGroupEdge;
using system_core::data::evaluatePredicate;
using system_core::data::GroupAssessDelegate;
using system_core::data::GroupLogic;
using system_core::data::toString;
using system_core::data::WatchAssessDelegate;
using system_core::data::WatchComputeDelegate;
using system_core::data::WatchDataType;
using system_core::data::WatchFunction;
using system_core::data::WatchpointGroup;
using system_core::data::WatchPredicate;

/* ----------------------------- Enum Tests ----------------------------- */

/** @test WatchPredicate enum has expected values. */
TEST(DataWatchpoint, PredicateEnumValues) {
  EXPECT_EQ(static_cast<std::uint8_t>(WatchPredicate::GT), 0);
  EXPECT_EQ(static_cast<std::uint8_t>(WatchPredicate::LT), 1);
  EXPECT_EQ(static_cast<std::uint8_t>(WatchPredicate::EQ), 4);
  EXPECT_EQ(static_cast<std::uint8_t>(WatchPredicate::CHANGED), 8);
  EXPECT_EQ(static_cast<std::uint8_t>(WatchPredicate::CUSTOM), 9);
}

/** @test WatchPredicate toString covers all values. */
TEST(DataWatchpoint, PredicateToString) {
  EXPECT_STREQ(toString(WatchPredicate::GT), "GT");
  EXPECT_STREQ(toString(WatchPredicate::LT), "LT");
  EXPECT_STREQ(toString(WatchPredicate::GE), "GE");
  EXPECT_STREQ(toString(WatchPredicate::LE), "LE");
  EXPECT_STREQ(toString(WatchPredicate::EQ), "EQ");
  EXPECT_STREQ(toString(WatchPredicate::NE), "NE");
  EXPECT_STREQ(toString(WatchPredicate::BIT_SET), "BIT_SET");
  EXPECT_STREQ(toString(WatchPredicate::BIT_CLEAR), "BIT_CLEAR");
  EXPECT_STREQ(toString(WatchPredicate::CHANGED), "CHANGED");
  EXPECT_STREQ(toString(WatchPredicate::CUSTOM), "CUSTOM");
}

/** @test WatchDataType toString covers all values. */
TEST(DataWatchpoint, DataTypeToString) {
  EXPECT_STREQ(toString(WatchDataType::UINT8), "UINT8");
  EXPECT_STREQ(toString(WatchDataType::UINT32), "UINT32");
  EXPECT_STREQ(toString(WatchDataType::INT32), "INT32");
  EXPECT_STREQ(toString(WatchDataType::FLOAT32), "FLOAT32");
  EXPECT_STREQ(toString(WatchDataType::FLOAT64), "FLOAT64");
  EXPECT_STREQ(toString(WatchDataType::RAW), "RAW");
}

/** @test GroupLogic toString covers all values. */
TEST(DataWatchpoint, GroupLogicToString) {
  EXPECT_STREQ(toString(GroupLogic::AND), "AND");
  EXPECT_STREQ(toString(GroupLogic::OR), "OR");
}

/* ----------------------------- dataTypeSize ----------------------------- */

/** @test dataTypeSize returns correct sizes. */
TEST(DataWatchpoint, DataTypeSize) {
  EXPECT_EQ(dataTypeSize(WatchDataType::UINT8), 1);
  EXPECT_EQ(dataTypeSize(WatchDataType::INT8), 1);
  EXPECT_EQ(dataTypeSize(WatchDataType::UINT16), 2);
  EXPECT_EQ(dataTypeSize(WatchDataType::INT16), 2);
  EXPECT_EQ(dataTypeSize(WatchDataType::UINT32), 4);
  EXPECT_EQ(dataTypeSize(WatchDataType::INT32), 4);
  EXPECT_EQ(dataTypeSize(WatchDataType::FLOAT32), 4);
  EXPECT_EQ(dataTypeSize(WatchDataType::UINT64), 8);
  EXPECT_EQ(dataTypeSize(WatchDataType::INT64), 8);
  EXPECT_EQ(dataTypeSize(WatchDataType::FLOAT64), 8);
  EXPECT_EQ(dataTypeSize(WatchDataType::RAW), 0);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default-constructed DataWatchpoint has expected defaults. */
TEST(DataWatchpoint, DefaultConstruction) {
  const DataWatchpoint WP{};
  EXPECT_EQ(WP.predicate, WatchPredicate::EQ);
  EXPECT_EQ(WP.dataType, WatchDataType::RAW);
  EXPECT_EQ(WP.eventId, 0);
  EXPECT_FALSE(WP.armed);
  EXPECT_FALSE(WP.lastResult);
  EXPECT_EQ(WP.fireCount, 0U);
  EXPECT_FALSE(static_cast<bool>(WP.customAssess));
}

/** @test WATCHPOINT_TABLE_SIZE matches Config. */
TEST(DataWatchpoint, TableSizeConstant) {
  EXPECT_EQ(system_core::data::WATCHPOINT_TABLE_SIZE, system_core::data::Config::WATCHPOINT_COUNT);
}

/** @test Default-constructed WatchpointGroup has expected defaults. */
TEST(DataWatchpoint, GroupDefaultConstruction) {
  const WatchpointGroup G{};
  EXPECT_EQ(G.count, 0);
  EXPECT_EQ(G.logic, GroupLogic::AND);
  EXPECT_EQ(G.eventId, 0);
  EXPECT_FALSE(G.armed);
  EXPECT_FALSE(G.lastResult);
  EXPECT_EQ(G.fireCount, 0U);
  EXPECT_FALSE(static_cast<bool>(G.customAssess));
}

/** @test WATCHPOINT_GROUP_TABLE_SIZE and WATCHPOINT_GROUP_MAX_REFS match Config. */
TEST(DataWatchpoint, GroupConstants) {
  EXPECT_EQ(system_core::data::WATCHPOINT_GROUP_TABLE_SIZE, system_core::data::Config::GROUP_COUNT);
  EXPECT_EQ(system_core::data::WATCHPOINT_GROUP_MAX_REFS,
            system_core::data::Config::GROUP_MAX_REFS);
}

/* ----------------------------- Float32 GT Predicate ----------------------------- */

/** @test Float32 GT evaluates correctly. */
TEST(DataWatchpoint, Float32GreaterThan) {
  DataWatchpoint wp{};
  wp.target = {0x007800, DataCategory::OUTPUT, 0, 4};
  wp.predicate = WatchPredicate::GT;
  wp.dataType = WatchDataType::FLOAT32;
  wp.armed = true;

  const float THRESHOLD = 100.0F;
  std::memcpy(wp.threshold.data(), &THRESHOLD, 4);

  // Value below threshold
  const float BELOW = 50.0F;
  EXPECT_FALSE(evaluatePredicate(wp, reinterpret_cast<const std::uint8_t*>(&BELOW), 4));

  // Value at threshold
  const float AT = 100.0F;
  EXPECT_FALSE(evaluatePredicate(wp, reinterpret_cast<const std::uint8_t*>(&AT), 4));

  // Value above threshold
  const float ABOVE = 150.0F;
  EXPECT_TRUE(evaluatePredicate(wp, reinterpret_cast<const std::uint8_t*>(&ABOVE), 4));
}

/* ----------------------------- Int32 LT Predicate ----------------------------- */

/** @test Int32 LT with negative values evaluates correctly. */
TEST(DataWatchpoint, Int32LessThanNegative) {
  DataWatchpoint wp{};
  wp.target = {0x007800, DataCategory::STATE, 0, 4};
  wp.predicate = WatchPredicate::LT;
  wp.dataType = WatchDataType::INT32;
  wp.armed = true;

  const std::int32_t THRESHOLD = -5;
  std::memcpy(wp.threshold.data(), &THRESHOLD, 4);

  const std::int32_t BELOW = -10;
  EXPECT_TRUE(evaluatePredicate(wp, reinterpret_cast<const std::uint8_t*>(&BELOW), 4));

  const std::int32_t ABOVE = 0;
  EXPECT_FALSE(evaluatePredicate(wp, reinterpret_cast<const std::uint8_t*>(&ABOVE), 4));
}

/* ----------------------------- Uint32 EQ Predicate ----------------------------- */

/** @test Uint32 EQ evaluates correctly. */
TEST(DataWatchpoint, Uint32Equal) {
  DataWatchpoint wp{};
  wp.target = {0x007800, DataCategory::STATE, 0, 4};
  wp.predicate = WatchPredicate::EQ;
  wp.dataType = WatchDataType::UINT32;
  wp.armed = true;

  const std::uint32_t THRESHOLD = 42;
  std::memcpy(wp.threshold.data(), &THRESHOLD, 4);

  const std::uint32_t MATCH = 42;
  EXPECT_TRUE(evaluatePredicate(wp, reinterpret_cast<const std::uint8_t*>(&MATCH), 4));

  const std::uint32_t NO_MATCH = 43;
  EXPECT_FALSE(evaluatePredicate(wp, reinterpret_cast<const std::uint8_t*>(&NO_MATCH), 4));
}

/* ----------------------------- GE / LE / NE ----------------------------- */

/** @test Float64 GE evaluates correctly. */
TEST(DataWatchpoint, Float64GreaterEqual) {
  DataWatchpoint wp{};
  wp.target = {0x007800, DataCategory::OUTPUT, 0, 8};
  wp.predicate = WatchPredicate::GE;
  wp.dataType = WatchDataType::FLOAT64;
  wp.armed = true;

  const double THRESHOLD = 100.0;
  std::memcpy(wp.threshold.data(), &THRESHOLD, 8);

  const double AT = 100.0;
  EXPECT_TRUE(evaluatePredicate(wp, reinterpret_cast<const std::uint8_t*>(&AT), 8));

  const double ABOVE = 100.1;
  EXPECT_TRUE(evaluatePredicate(wp, reinterpret_cast<const std::uint8_t*>(&ABOVE), 8));

  const double BELOW = 99.9;
  EXPECT_FALSE(evaluatePredicate(wp, reinterpret_cast<const std::uint8_t*>(&BELOW), 8));
}

/** @test Uint8 NE evaluates correctly. */
TEST(DataWatchpoint, Uint8NotEqual) {
  DataWatchpoint wp{};
  wp.target = {0x007900, DataCategory::STATE, 0, 1};
  wp.predicate = WatchPredicate::NE;
  wp.dataType = WatchDataType::UINT8;
  wp.armed = true;

  const std::uint8_t THRESHOLD = 0;
  wp.threshold[0] = THRESHOLD;

  const std::uint8_t ZERO = 0;
  EXPECT_FALSE(evaluatePredicate(wp, &ZERO, 1));

  const std::uint8_t NON_ZERO = 5;
  EXPECT_TRUE(evaluatePredicate(wp, &NON_ZERO, 1));
}

/* ----------------------------- BIT_SET / BIT_CLEAR ----------------------------- */

/** @test BIT_SET predicate checks specific bits. */
TEST(DataWatchpoint, BitSet) {
  DataWatchpoint wp{};
  wp.target = {0x007800, DataCategory::STATE, 0, 1};
  wp.predicate = WatchPredicate::BIT_SET;
  wp.armed = true;

  wp.threshold[0] = 0x03; // Check bits 0 and 1

  const std::uint8_t ALL_SET = 0xFF;
  EXPECT_TRUE(evaluatePredicate(wp, &ALL_SET, 1));

  const std::uint8_t PARTIAL = 0x01; // Only bit 0 set
  EXPECT_FALSE(evaluatePredicate(wp, &PARTIAL, 1));

  const std::uint8_t EXACT = 0x03;
  EXPECT_TRUE(evaluatePredicate(wp, &EXACT, 1));
}

/** @test BIT_CLEAR predicate checks specific bits are clear. */
TEST(DataWatchpoint, BitClear) {
  DataWatchpoint wp{};
  wp.target = {0x007800, DataCategory::STATE, 0, 1};
  wp.predicate = WatchPredicate::BIT_CLEAR;
  wp.armed = true;

  wp.threshold[0] = 0x0F; // Check low nibble is clear

  const std::uint8_t CLEAR = 0xF0;
  EXPECT_TRUE(evaluatePredicate(wp, &CLEAR, 1));

  const std::uint8_t NOT_CLEAR = 0xF1;
  EXPECT_FALSE(evaluatePredicate(wp, &NOT_CLEAR, 1));
}

/* ----------------------------- CHANGED Predicate ----------------------------- */

/** @test CHANGED predicate detects value changes. */
TEST(DataWatchpoint, Changed) {
  DataWatchpoint wp{};
  wp.target = {0x007800, DataCategory::STATE, 0, 4};
  wp.predicate = WatchPredicate::CHANGED;
  wp.armed = true;

  const std::uint32_t INITIAL = 100;
  std::memcpy(wp.lastValue.data(), &INITIAL, 4);

  // Same value -> not changed
  EXPECT_FALSE(evaluatePredicate(wp, reinterpret_cast<const std::uint8_t*>(&INITIAL), 4));

  // Different value -> changed
  const std::uint32_t DIFFERENT = 200;
  EXPECT_TRUE(evaluatePredicate(wp, reinterpret_cast<const std::uint8_t*>(&DIFFERENT), 4));
}

/* ----------------------------- RAW Comparison ----------------------------- */

/** @test RAW EQ comparison uses memcmp. */
TEST(DataWatchpoint, RawEqual) {
  DataWatchpoint wp{};
  wp.target = {0x007800, DataCategory::STATE, 0, 3};
  wp.predicate = WatchPredicate::EQ;
  wp.dataType = WatchDataType::RAW;
  wp.armed = true;

  wp.threshold[0] = 0xAA;
  wp.threshold[1] = 0xBB;
  wp.threshold[2] = 0xCC;

  const std::uint8_t MATCH[] = {0xAA, 0xBB, 0xCC};
  EXPECT_TRUE(evaluatePredicate(wp, MATCH, 3));

  const std::uint8_t NO_MATCH[] = {0xAA, 0xBB, 0xDD};
  EXPECT_FALSE(evaluatePredicate(wp, NO_MATCH, 3));
}

/* ----------------------------- Null Safety ----------------------------- */

/** @test evaluatePredicate returns false for null data. */
TEST(DataWatchpoint, NullDataReturnsFalse) {
  DataWatchpoint wp{};
  wp.predicate = WatchPredicate::EQ;
  wp.dataType = WatchDataType::UINT32;
  wp.armed = true;

  EXPECT_FALSE(evaluatePredicate(wp, nullptr, 4));
}

/** @test evaluatePredicate returns false for zero-length data. */
TEST(DataWatchpoint, ZeroLengthReturnsFalse) {
  const std::uint8_t DUMMY = 0;
  DataWatchpoint wp{};
  wp.predicate = WatchPredicate::EQ;
  wp.armed = true;

  EXPECT_FALSE(evaluatePredicate(wp, &DUMMY, 0));
}

/* ----------------------------- Custom Predicate ----------------------------- */

/** @test Custom delegate is invoked when predicate is CUSTOM. */
TEST(DataWatchpoint, CustomDelegateInvoked) {
  // Context: threshold stored externally
  struct Ctx {
    float threshold;
  };
  Ctx ctx{100.0F};

  // Custom predicate: interpret data as float, check > threshold
  auto customFn = [](void* c, const std::uint8_t* data, std::size_t /*len*/) noexcept -> bool {
    auto* cx = static_cast<Ctx*>(c);
    float val{};
    std::memcpy(&val, data, sizeof(float));
    return val > cx->threshold;
  };

  DataWatchpoint wp{};
  wp.target = {0x007800, DataCategory::OUTPUT, 0, 4};
  wp.predicate = WatchPredicate::CUSTOM;
  wp.customAssess = {customFn, &ctx};
  wp.armed = true;

  const float BELOW = 50.0F;
  EXPECT_FALSE(evaluatePredicate(wp, reinterpret_cast<const std::uint8_t*>(&BELOW), 4));

  const float ABOVE = 150.0F;
  EXPECT_TRUE(evaluatePredicate(wp, reinterpret_cast<const std::uint8_t*>(&ABOVE), 4));
}

/** @test Custom delegate returning false keeps lastResult false. */
TEST(DataWatchpoint, CustomDelegateReturnsFalse) {
  auto alwaysFalse = [](void*, const std::uint8_t*, std::size_t) noexcept -> bool { return false; };

  DataWatchpoint wp{};
  wp.predicate = WatchPredicate::CUSTOM;
  wp.customAssess = {alwaysFalse, nullptr};
  wp.armed = true;

  const std::uint8_t DATA[] = {0xFF, 0xFF, 0xFF, 0xFF};
  EXPECT_FALSE(evaluatePredicate(wp, DATA, 4));
}

/** @test CUSTOM predicate with null delegate returns false. */
TEST(DataWatchpoint, CustomNullDelegateReturnsFalse) {
  DataWatchpoint wp{};
  wp.predicate = WatchPredicate::CUSTOM;
  // customAssess left default (null)
  wp.armed = true;

  const std::uint8_t DATA[] = {0xFF};
  EXPECT_FALSE(evaluatePredicate(wp, DATA, 1));
}

/** @test Custom delegate with edge detection fires correctly. */
TEST(DataWatchpoint, CustomDelegateEdgeDetection) {
  // Context tracks a call count and alternates result
  struct Ctx {
    float threshold;
  };
  Ctx ctx{100.0F};

  auto customFn = [](void* c, const std::uint8_t* data, std::size_t) noexcept -> bool {
    auto* cx = static_cast<Ctx*>(c);
    float val{};
    std::memcpy(&val, data, sizeof(float));
    return val > cx->threshold;
  };

  DataWatchpoint wp{};
  wp.target = {0x007800, DataCategory::OUTPUT, 0, 4};
  wp.predicate = WatchPredicate::CUSTOM;
  wp.customAssess = {customFn, &ctx};
  wp.eventId = 10;
  wp.armed = true;

  // Below threshold: no fire
  const float BELOW = 50.0F;
  EXPECT_FALSE(evaluateEdge(wp, reinterpret_cast<const std::uint8_t*>(&BELOW), 4));
  EXPECT_EQ(wp.fireCount, 0U);

  // Above threshold: fire (false -> true)
  const float ABOVE = 150.0F;
  EXPECT_TRUE(evaluateEdge(wp, reinterpret_cast<const std::uint8_t*>(&ABOVE), 4));
  EXPECT_EQ(wp.fireCount, 1U);

  // Still above: no fire (already true)
  EXPECT_FALSE(evaluateEdge(wp, reinterpret_cast<const std::uint8_t*>(&ABOVE), 4));
  EXPECT_EQ(wp.fireCount, 1U);

  // Below again: no fire (true -> false, debounce resets fireCount)
  EXPECT_FALSE(evaluateEdge(wp, reinterpret_cast<const std::uint8_t*>(&BELOW), 4));
  EXPECT_EQ(wp.fireCount, 0U);

  // Above again: fire (false -> true, fireCount restarts from 0)
  EXPECT_TRUE(evaluateEdge(wp, reinterpret_cast<const std::uint8_t*>(&ABOVE), 4));
  EXPECT_EQ(wp.fireCount, 1U);
}

/** @test Custom delegate context can be updated between evaluations. */
TEST(DataWatchpoint, CustomDelegateContextUpdate) {
  struct Ctx {
    float threshold;
  };
  Ctx ctx{100.0F};

  auto customFn = [](void* c, const std::uint8_t* data, std::size_t) noexcept -> bool {
    auto* cx = static_cast<Ctx*>(c);
    float val{};
    std::memcpy(&val, data, sizeof(float));
    return val > cx->threshold;
  };

  DataWatchpoint wp{};
  wp.predicate = WatchPredicate::CUSTOM;
  wp.customAssess = {customFn, &ctx};
  wp.armed = true;

  const float VAL = 75.0F;
  // 75 > 100 => false
  EXPECT_FALSE(evaluatePredicate(wp, reinterpret_cast<const std::uint8_t*>(&VAL), 4));

  // Lower the threshold via context
  ctx.threshold = 50.0F;
  // 75 > 50 => true
  EXPECT_TRUE(evaluatePredicate(wp, reinterpret_cast<const std::uint8_t*>(&VAL), 4));
}

/* ----------------------------- Edge Detection ----------------------------- */

/** @test evaluateEdge fires only on false->true transition. */
TEST(DataWatchpoint, EdgeDetection) {
  DataWatchpoint wp{};
  wp.target = {0x007800, DataCategory::OUTPUT, 0, 4};
  wp.predicate = WatchPredicate::GT;
  wp.dataType = WatchDataType::FLOAT32;
  wp.eventId = 42;
  wp.armed = true;

  const float THRESHOLD = 100.0F;
  std::memcpy(wp.threshold.data(), &THRESHOLD, 4);

  // Below threshold: no fire
  const float BELOW = 50.0F;
  EXPECT_FALSE(evaluateEdge(wp, reinterpret_cast<const std::uint8_t*>(&BELOW), 4));
  EXPECT_EQ(wp.fireCount, 0U);
  EXPECT_FALSE(wp.lastResult);

  // Above threshold: fire (false -> true transition)
  const float ABOVE = 150.0F;
  EXPECT_TRUE(evaluateEdge(wp, reinterpret_cast<const std::uint8_t*>(&ABOVE), 4));
  EXPECT_EQ(wp.fireCount, 1U);
  EXPECT_TRUE(wp.lastResult);

  // Still above: no fire (already true, no transition)
  const float STILL_ABOVE = 200.0F;
  EXPECT_FALSE(evaluateEdge(wp, reinterpret_cast<const std::uint8_t*>(&STILL_ABOVE), 4));
  EXPECT_EQ(wp.fireCount, 1U);

  // Below again: no fire (true -> false transition, debounce resets fireCount)
  EXPECT_FALSE(evaluateEdge(wp, reinterpret_cast<const std::uint8_t*>(&BELOW), 4));
  EXPECT_EQ(wp.fireCount, 0U);
  EXPECT_FALSE(wp.lastResult);

  // Above again: fire again (false -> true transition, fireCount restarts)
  EXPECT_TRUE(evaluateEdge(wp, reinterpret_cast<const std::uint8_t*>(&ABOVE), 4));
  EXPECT_EQ(wp.fireCount, 1U);
}

/** @test evaluateEdge does not fire when unarmed. */
TEST(DataWatchpoint, EdgeUnarmedNoFire) {
  DataWatchpoint wp{};
  wp.predicate = WatchPredicate::GT;
  wp.dataType = WatchDataType::FLOAT32;
  wp.armed = false;

  const float THRESHOLD = 0.0F;
  std::memcpy(wp.threshold.data(), &THRESHOLD, 4);

  const float ABOVE = 100.0F;
  EXPECT_FALSE(evaluateEdge(wp, reinterpret_cast<const std::uint8_t*>(&ABOVE), 4));
  EXPECT_EQ(wp.fireCount, 0U);
}

/** @test evaluateEdge updates lastValue for CHANGED predicate. */
TEST(DataWatchpoint, EdgeChangedUpdatesLastValue) {
  DataWatchpoint wp{};
  wp.target = {0x007800, DataCategory::STATE, 0, 4};
  wp.predicate = WatchPredicate::CHANGED;
  wp.armed = true;

  const std::uint32_t VAL_A = 100;
  std::memcpy(wp.lastValue.data(), &VAL_A, 4);

  // Different value -> fires and updates lastValue
  const std::uint32_t VAL_B = 200;
  EXPECT_TRUE(evaluateEdge(wp, reinterpret_cast<const std::uint8_t*>(&VAL_B), 4));
  EXPECT_EQ(wp.fireCount, 1U);

  // Check lastValue was updated to VAL_B
  std::uint32_t stored = 0;
  std::memcpy(&stored, wp.lastValue.data(), 4);
  EXPECT_EQ(stored, 200U);

  // Same value (VAL_B) again -> not changed, no fire
  EXPECT_FALSE(evaluateEdge(wp, reinterpret_cast<const std::uint8_t*>(&VAL_B), 4));
}

/* ----------------------------- WatchpointGroup AND ----------------------------- */

/** @test Group AND logic requires all referenced watchpoints true. */
TEST(DataWatchpoint, GroupAndAllTrue) {
  // Set up a watchpoint table with 3 entries
  DataWatchpoint table[3]{};
  table[0].watchpointId = 1;
  table[0].armed = true;
  table[0].lastResult = true;
  table[1].watchpointId = 2;
  table[1].armed = true;
  table[1].lastResult = true;
  table[2].watchpointId = 3;
  table[2].armed = true;
  table[2].lastResult = true;

  WatchpointGroup group{};
  group.refs = {1, 2, 3, 0};
  group.count = 3;
  group.logic = GroupLogic::AND;
  group.armed = true;

  EXPECT_TRUE(evaluateGroup(group, table, 3));
}

/** @test Group AND logic fails when any watchpoint is false. */
TEST(DataWatchpoint, GroupAndOneFalse) {
  DataWatchpoint table[3]{};
  table[0].watchpointId = 1;
  table[0].armed = true;
  table[0].lastResult = true;
  table[1].watchpointId = 2;
  table[1].armed = true;
  table[1].lastResult = false;
  table[2].watchpointId = 3;
  table[2].armed = true;
  table[2].lastResult = true;

  WatchpointGroup group{};
  group.refs = {1, 2, 3, 0};
  group.count = 3;
  group.logic = GroupLogic::AND;
  group.armed = true;

  EXPECT_FALSE(evaluateGroup(group, table, 3));
}

/** @test Group AND logic fails when all watchpoints are false. */
TEST(DataWatchpoint, GroupAndAllFalse) {
  DataWatchpoint table[2]{};
  table[0].watchpointId = 1;
  table[0].armed = true;
  table[0].lastResult = false;
  table[1].watchpointId = 2;
  table[1].armed = true;
  table[1].lastResult = false;

  WatchpointGroup group{};
  group.refs = {1, 2, 0, 0};
  group.count = 2;
  group.logic = GroupLogic::AND;
  group.armed = true;

  EXPECT_FALSE(evaluateGroup(group, table, 2));
}

/* ----------------------------- WatchpointGroup OR ----------------------------- */

/** @test Group OR logic passes when any watchpoint is true. */
TEST(DataWatchpoint, GroupOrOneTrue) {
  DataWatchpoint table[3]{};
  table[0].watchpointId = 1;
  table[0].armed = true;
  table[0].lastResult = false;
  table[1].watchpointId = 2;
  table[1].armed = true;
  table[1].lastResult = true;
  table[2].watchpointId = 3;
  table[2].armed = true;
  table[2].lastResult = false;

  WatchpointGroup group{};
  group.refs = {1, 2, 3, 0};
  group.count = 3;
  group.logic = GroupLogic::OR;
  group.armed = true;

  EXPECT_TRUE(evaluateGroup(group, table, 3));
}

/** @test Group OR logic fails when all watchpoints are false. */
TEST(DataWatchpoint, GroupOrAllFalse) {
  DataWatchpoint table[2]{};
  table[0].watchpointId = 1;
  table[0].armed = true;
  table[0].lastResult = false;
  table[1].watchpointId = 2;
  table[1].armed = true;
  table[1].lastResult = false;

  WatchpointGroup group{};
  group.refs = {1, 2, 0, 0};
  group.count = 2;
  group.logic = GroupLogic::OR;
  group.armed = true;

  EXPECT_FALSE(evaluateGroup(group, table, 2));
}

/** @test Group OR logic passes when all watchpoints are true. */
TEST(DataWatchpoint, GroupOrAllTrue) {
  DataWatchpoint table[2]{};
  table[0].watchpointId = 1;
  table[0].armed = true;
  table[0].lastResult = true;
  table[1].watchpointId = 2;
  table[1].armed = true;
  table[1].lastResult = true;

  WatchpointGroup group{};
  group.refs = {1, 2, 0, 0};
  group.count = 2;
  group.logic = GroupLogic::OR;
  group.armed = true;

  EXPECT_TRUE(evaluateGroup(group, table, 2));
}

/* ----------------------------- Group Edge Detection ----------------------------- */

/** @test Group edge detection fires on false->true transition. */
TEST(DataWatchpoint, GroupEdgeDetection) {
  DataWatchpoint table[2]{};
  table[0].watchpointId = 1;
  table[0].armed = true;
  table[1].watchpointId = 2;
  table[1].armed = true;

  WatchpointGroup group{};
  group.refs = {1, 2, 0, 0};
  group.count = 2;
  group.logic = GroupLogic::AND;
  group.eventId = 5;
  group.armed = true;

  // Both false: group is false, no fire
  table[0].lastResult = false;
  table[1].lastResult = false;
  EXPECT_FALSE(evaluateGroupEdge(group, table, 2));
  EXPECT_EQ(group.fireCount, 0U);

  // One true: group still false (AND), no fire
  table[0].lastResult = true;
  EXPECT_FALSE(evaluateGroupEdge(group, table, 2));
  EXPECT_EQ(group.fireCount, 0U);

  // Both true: group goes true, fire (false -> true)
  table[1].lastResult = true;
  EXPECT_TRUE(evaluateGroupEdge(group, table, 2));
  EXPECT_EQ(group.fireCount, 1U);
  EXPECT_TRUE(group.lastResult);

  // Still both true: no fire (already true)
  EXPECT_FALSE(evaluateGroupEdge(group, table, 2));
  EXPECT_EQ(group.fireCount, 1U);

  // One goes false: group goes false, no fire
  table[0].lastResult = false;
  EXPECT_FALSE(evaluateGroupEdge(group, table, 2));
  EXPECT_FALSE(group.lastResult);

  // Both true again: fire again
  table[0].lastResult = true;
  EXPECT_TRUE(evaluateGroupEdge(group, table, 2));
  EXPECT_EQ(group.fireCount, 2U);
}

/** @test Group edge detection does not fire when unarmed. */
TEST(DataWatchpoint, GroupEdgeUnarmed) {
  DataWatchpoint table[1]{};
  table[0].watchpointId = 1;
  table[0].armed = true;
  table[0].lastResult = true;

  WatchpointGroup group{};
  group.refs = {1, 0, 0, 0};
  group.count = 1;
  group.logic = GroupLogic::AND;
  group.armed = false;

  EXPECT_FALSE(evaluateGroupEdge(group, table, 1));
  EXPECT_EQ(group.fireCount, 0U);
}

/* ----------------------------- Group Null Safety ----------------------------- */

/** @test Group evaluation returns false for null table. */
TEST(DataWatchpoint, GroupNullTable) {
  WatchpointGroup group{};
  group.count = 1;
  group.refs = {1, 0, 0, 0};
  group.logic = GroupLogic::AND;

  EXPECT_FALSE(evaluateGroup(group, nullptr, 0));
}

/** @test Group evaluation returns false for zero count. */
TEST(DataWatchpoint, GroupZeroCount) {
  DataWatchpoint table[1]{};
  table[0].watchpointId = 1;
  table[0].armed = true;
  table[0].lastResult = true;

  WatchpointGroup group{};
  group.count = 0;
  group.logic = GroupLogic::AND;

  EXPECT_FALSE(evaluateGroup(group, table, 1));
}

/** @test Group handles out-of-bounds index gracefully (treats as false). */
TEST(DataWatchpoint, GroupOutOfBoundsIndex) {
  DataWatchpoint table[2]{};
  table[0].watchpointId = 1;
  table[0].armed = true;
  table[0].lastResult = true;
  table[1].watchpointId = 2;
  table[1].armed = true;
  table[1].lastResult = true;

  WatchpointGroup group{};
  group.refs = {1, 99, 0, 0}; // ID 99 does not exist in the table
  group.count = 2;
  group.logic = GroupLogic::AND;

  // ID 99 resolves to false (no match), so AND fails
  EXPECT_FALSE(evaluateGroup(group, table, 2));
}

/* ----------------------------- Group Custom Delegate ----------------------------- */

/** @test Group custom delegate overrides AND/OR logic. */
TEST(DataWatchpoint, GroupCustomDelegate) {
  DataWatchpoint table[2]{};
  table[0].watchpointId = 1;
  table[0].armed = true;
  table[0].lastResult = true;
  table[1].watchpointId = 2;
  table[1].armed = true;
  table[1].lastResult = false;

  // Custom: always returns true regardless of individual results
  auto alwaysTrue = [](void*, const bool*, std::uint8_t) noexcept -> bool { return true; };

  WatchpointGroup group{};
  group.refs = {1, 2, 0, 0};
  group.count = 2;
  group.logic = GroupLogic::AND; // Would normally be false (ID 2 is false)
  group.customAssess = {alwaysTrue, nullptr};
  group.armed = true;

  // Custom overrides AND logic
  EXPECT_TRUE(evaluateGroup(group, table, 2));
}

/** @test Group custom delegate receives correct results array. */
TEST(DataWatchpoint, GroupCustomDelegateReceivesResults) {
  DataWatchpoint table[3]{};
  table[0].watchpointId = 1;
  table[0].armed = true;
  table[0].lastResult = true;
  table[1].watchpointId = 2;
  table[1].armed = true;
  table[1].lastResult = false;
  table[2].watchpointId = 3;
  table[2].armed = true;
  table[2].lastResult = true;

  struct Ctx {
    bool receivedResults[4]{};
    std::uint8_t receivedCount{0};
  };
  Ctx ctx{};

  // Custom: captures the results for verification
  auto capture = [](void* c, const bool* results, std::uint8_t count) noexcept -> bool {
    auto* cx = static_cast<Ctx*>(c);
    cx->receivedCount = count;
    for (std::uint8_t i = 0; i < count; ++i) {
      cx->receivedResults[i] = results[i];
    }
    return true;
  };

  WatchpointGroup group{};
  group.refs = {1, 2, 3, 0};
  group.count = 3;
  group.customAssess = {capture, &ctx};

  EXPECT_TRUE(evaluateGroup(group, table, 3));

  EXPECT_EQ(ctx.receivedCount, 3);
  EXPECT_TRUE(ctx.receivedResults[0]);
  EXPECT_FALSE(ctx.receivedResults[1]);
  EXPECT_TRUE(ctx.receivedResults[2]);
}

/** @test Group custom delegate for cross-variable equation: var0 + 3*var1 > 50. */
TEST(DataWatchpoint, GroupCustomEquation) {
  // Set up two watchpoints monitoring float values
  // The watchpoints themselves just track the values via lastValue
  DataWatchpoint table[2]{};
  table[0].watchpointId = 1;
  table[0].armed = true;
  table[0].target = {0x007800, DataCategory::OUTPUT, 0, 4};
  table[0].dataType = WatchDataType::FLOAT32;
  table[0].lastResult = true; // Must be true for group to consider them

  table[1].watchpointId = 2;
  table[1].armed = true;
  table[1].target = {0x007800, DataCategory::OUTPUT, 4, 4};
  table[1].dataType = WatchDataType::FLOAT32;
  table[1].lastResult = true;

  // Store actual float values in lastValue for the delegate to read
  const float VAR0 = 20.0F;
  const float VAR1 = 15.0F;
  std::memcpy(table[0].lastValue.data(), &VAR0, 4);
  std::memcpy(table[1].lastValue.data(), &VAR1, 4);

  // Context holds pointer to the watchpoint table for value access
  struct EqCtx {
    const DataWatchpoint* table;
    const std::uint8_t* indices;
  };
  EqCtx eqCtx{table, nullptr};

  // Equation: var0 + 3 * var1 > 50
  // 20 + 3*15 = 65 > 50 => true
  auto equation = [](void* c, const bool*, std::uint8_t) noexcept -> bool {
    auto* cx = static_cast<EqCtx*>(c);
    float v0{};
    float v1{};
    std::memcpy(&v0, cx->table[0].lastValue.data(), 4);
    std::memcpy(&v1, cx->table[1].lastValue.data(), 4);
    return (v0 + 3.0F * v1) > 50.0F;
  };

  WatchpointGroup group{};
  group.refs = {1, 2, 0, 0};
  group.count = 2;
  group.customAssess = {equation, &eqCtx};
  group.armed = true;

  // 20 + 3*15 = 65 > 50 => true
  EXPECT_TRUE(evaluateGroup(group, table, 2));

  // Change var1 to 5: 20 + 3*5 = 35 > 50 => false
  const float VAR1_LOW = 5.0F;
  std::memcpy(table[1].lastValue.data(), &VAR1_LOW, 4);
  EXPECT_FALSE(evaluateGroup(group, table, 2));

  // Change var0 to 40: 40 + 3*5 = 55 > 50 => true
  const float VAR0_HIGH = 40.0F;
  std::memcpy(table[0].lastValue.data(), &VAR0_HIGH, 4);
  EXPECT_TRUE(evaluateGroup(group, table, 2));
}

/** @test Group custom equation with edge detection fires on transition. */
TEST(DataWatchpoint, GroupCustomEquationEdge) {
  DataWatchpoint table[2]{};
  table[0].watchpointId = 1;
  table[0].armed = true;
  table[0].lastResult = true;
  table[1].watchpointId = 2;
  table[1].armed = true;
  table[1].lastResult = true;

  const float VAR0 = 10.0F;
  const float VAR1 = 5.0F;
  std::memcpy(table[0].lastValue.data(), &VAR0, 4);
  std::memcpy(table[1].lastValue.data(), &VAR1, 4);

  struct EqCtx {
    const DataWatchpoint* table;
  };
  EqCtx eqCtx{table};

  // var0 + 3*var1 > 50
  auto equation = [](void* c, const bool*, std::uint8_t) noexcept -> bool {
    auto* cx = static_cast<EqCtx*>(c);
    float v0{};
    float v1{};
    std::memcpy(&v0, cx->table[0].lastValue.data(), 4);
    std::memcpy(&v1, cx->table[1].lastValue.data(), 4);
    return (v0 + 3.0F * v1) > 50.0F;
  };

  WatchpointGroup group{};
  group.refs = {1, 2, 0, 0};
  group.count = 2;
  group.customAssess = {equation, &eqCtx};
  group.eventId = 99;
  group.armed = true;

  // 10 + 3*5 = 25 > 50 => false, no fire
  EXPECT_FALSE(evaluateGroupEdge(group, table, 2));
  EXPECT_EQ(group.fireCount, 0U);

  // Change var1 to 20: 10 + 3*20 = 70 > 50 => true, fire!
  const float VAR1_HIGH = 20.0F;
  std::memcpy(table[1].lastValue.data(), &VAR1_HIGH, 4);
  EXPECT_TRUE(evaluateGroupEdge(group, table, 2));
  EXPECT_EQ(group.fireCount, 1U);

  // Still true, no fire
  EXPECT_FALSE(evaluateGroupEdge(group, table, 2));
  EXPECT_EQ(group.fireCount, 1U);

  // Drop below: 10 + 3*5 = 25, no fire (true -> false)
  std::memcpy(table[1].lastValue.data(), &VAR1, 4);
  EXPECT_FALSE(evaluateGroupEdge(group, table, 2));
  EXPECT_FALSE(group.lastResult);

  // Back above: fire again
  std::memcpy(table[1].lastValue.data(), &VAR1_HIGH, 4);
  EXPECT_TRUE(evaluateGroupEdge(group, table, 2));
  EXPECT_EQ(group.fireCount, 2U);
}

/** @test Group with single watchpoint behaves like individual. */
TEST(DataWatchpoint, GroupSingleWatchpoint) {
  DataWatchpoint table[1]{};
  table[0].watchpointId = 1;
  table[0].armed = true;
  table[0].lastResult = true;

  WatchpointGroup group{};
  group.refs = {1, 0, 0, 0};
  group.count = 1;
  group.logic = GroupLogic::AND;
  group.armed = true;

  EXPECT_TRUE(evaluateGroup(group, table, 1));

  table[0].lastResult = false;
  EXPECT_FALSE(evaluateGroup(group, table, 1));
}

/* ----------------------------- Debounce (minFireCount) ----------------------------- */

/** @test Default minFireCount is 0 (no debounce). */
TEST(DataWatchpoint, DefaultMinFireCount) {
  DataWatchpoint wp{};
  EXPECT_EQ(wp.minFireCount, 0U);
}

/** @test With minFireCount=0, evaluateEdge fires on first edge. */
TEST(DataWatchpoint, DebounceZeroFiresImmediately) {
  DataWatchpoint wp{};
  wp.predicate = WatchPredicate::GT;
  wp.dataType = WatchDataType::UINT8;
  wp.minFireCount = 0;
  wp.armed = true;

  std::uint8_t threshold = 10;
  std::memcpy(wp.threshold.data(), &threshold, 1);

  std::uint8_t val = 20;
  EXPECT_TRUE(evaluateEdge(wp, &val, 1));
  EXPECT_EQ(wp.fireCount, 1U);
}

/** @test With minFireCount=3, event dispatches only on 3rd edge. */
TEST(DataWatchpoint, DebounceRequiresThreeEdges) {
  DataWatchpoint wp{};
  wp.predicate = WatchPredicate::GT;
  wp.dataType = WatchDataType::UINT8;
  wp.minFireCount = 3;
  wp.armed = true;

  std::uint8_t threshold = 10;
  std::memcpy(wp.threshold.data(), &threshold, 1);

  std::uint8_t high = 20;
  std::uint8_t low = 5;

  // Edge 1: fire but no dispatch
  EXPECT_FALSE(evaluateEdge(wp, &high, 1));
  EXPECT_EQ(wp.fireCount, 1U);

  // Go low to reset edge
  EXPECT_FALSE(evaluateEdge(wp, &low, 1));
  EXPECT_EQ(wp.fireCount, 0U); // Reset on false

  // Edge 2
  EXPECT_FALSE(evaluateEdge(wp, &high, 1));
  EXPECT_EQ(wp.fireCount, 1U);
  EXPECT_FALSE(evaluateEdge(wp, &low, 1));

  // Edge 3
  EXPECT_FALSE(evaluateEdge(wp, &high, 1));
  EXPECT_EQ(wp.fireCount, 1U);
  EXPECT_FALSE(evaluateEdge(wp, &low, 1));

  // Edge 4
  EXPECT_FALSE(evaluateEdge(wp, &high, 1));
  EXPECT_EQ(wp.fireCount, 1U);
}

/** @test Debounce with sustained true: count stays at 1 (only edges count). */
TEST(DataWatchpoint, DebounceSustainedTrue) {
  DataWatchpoint wp{};
  wp.predicate = WatchPredicate::GT;
  wp.dataType = WatchDataType::UINT8;
  wp.minFireCount = 3;
  wp.armed = true;

  std::uint8_t threshold = 10;
  std::memcpy(wp.threshold.data(), &threshold, 1);

  std::uint8_t high = 20;

  // First edge
  EXPECT_FALSE(evaluateEdge(wp, &high, 1));
  EXPECT_EQ(wp.fireCount, 1U);

  // Sustained true: no more edges, count stays at 1
  EXPECT_FALSE(evaluateEdge(wp, &high, 1));
  EXPECT_EQ(wp.fireCount, 1U);
  EXPECT_FALSE(evaluateEdge(wp, &high, 1));
  EXPECT_EQ(wp.fireCount, 1U);
}

/** @test Debounce resets fireCount when predicate goes false. */
TEST(DataWatchpoint, DebounceResetsOnFalse) {
  DataWatchpoint wp{};
  wp.predicate = WatchPredicate::GT;
  wp.dataType = WatchDataType::UINT8;
  wp.minFireCount = 2;
  wp.armed = true;

  std::uint8_t threshold = 10;
  std::memcpy(wp.threshold.data(), &threshold, 1);

  std::uint8_t high = 20;
  std::uint8_t low = 5;

  // Edge 1
  EXPECT_FALSE(evaluateEdge(wp, &high, 1));
  EXPECT_EQ(wp.fireCount, 1U);

  // Goes false: resets
  EXPECT_FALSE(evaluateEdge(wp, &low, 1));
  EXPECT_EQ(wp.fireCount, 0U);

  // Edge 1 again (counter reset)
  EXPECT_FALSE(evaluateEdge(wp, &high, 1));
  EXPECT_EQ(wp.fireCount, 1U);
}

/* ----------------------------- WatchFunction ----------------------------- */

/** @test WatchFunction::NONE preserves raw evaluation behavior. */
TEST(DataWatchpoint, WatchFunctionNoneIsDefault) {
  DataWatchpoint wp{};
  EXPECT_EQ(wp.function, WatchFunction::NONE);

  wp.predicate = WatchPredicate::GT;
  wp.dataType = WatchDataType::FLOAT32;
  wp.function = WatchFunction::NONE;
  wp.armed = true;
  float threshold = 50.0F;
  std::memcpy(wp.threshold.data(), &threshold, 4);

  float value = 60.0F;
  std::uint8_t bytes[4];
  std::memcpy(bytes, &value, 4);

  EXPECT_TRUE(evaluateEdge(wp, bytes, 4));
}

/** @test WatchFunction::DELTA computes absolute change. */
TEST(DataWatchpoint, WatchFunctionDelta) {
  DataWatchpoint wp{};
  wp.predicate = WatchPredicate::GT;
  wp.dataType = WatchDataType::FLOAT32;
  wp.function = WatchFunction::DELTA;
  wp.armed = true;

  // Threshold: delta > 5.0
  double threshold = 5.0;
  std::memcpy(wp.threshold.data(), &threshold, sizeof(double));

  float v1 = 10.0F;
  float v2 = 12.0F;
  float v3 = 20.0F;
  std::uint8_t bytes[4];

  // First call: previousValue is 0, delta = |10 - 0| = 10 > 5 -> fire
  std::memcpy(bytes, &v1, 4);
  EXPECT_TRUE(evaluateEdge(wp, bytes, 4));

  // delta = |12 - 10| = 2 < 5 -> false (edge: true->false)
  std::memcpy(bytes, &v2, 4);
  EXPECT_FALSE(evaluateEdge(wp, bytes, 4));

  // delta = |20 - 12| = 8 > 5 -> true (edge: false->true, fire)
  std::memcpy(bytes, &v3, 4);
  EXPECT_TRUE(evaluateEdge(wp, bytes, 4));
}

/** @test WatchFunction::RATE computes rate of change per second. */
TEST(DataWatchpoint, WatchFunctionRate) {
  DataWatchpoint wp{};
  wp.predicate = WatchPredicate::GT;
  wp.dataType = WatchDataType::FLOAT32;
  wp.function = WatchFunction::RATE;
  wp.armed = true;

  // Threshold: rate > 100.0 per second
  double threshold = 100.0;
  std::memcpy(wp.threshold.data(), &threshold, sizeof(double));

  // dt = 0.01s (100 Hz), value jumps from 0 to 5 -> rate = 500/s
  float v1 = 0.0F;
  float v2 = 5.0F;
  std::uint8_t bytes[4];

  std::memcpy(bytes, &v1, 4);
  applyWatchFunction(wp, bytes, 4, 0.01); // Prime previousValue

  std::memcpy(bytes, &v2, 4);
  const double RATE = applyWatchFunction(wp, bytes, 4, 0.01);
  EXPECT_NEAR(RATE, 500.0, 0.01);
}

/** @test WatchFunction::MAGNITUDE computes vector magnitude across fields. */
TEST(DataWatchpoint, WatchFunctionMagnitude) {
  DataWatchpoint wp{};
  wp.predicate = WatchPredicate::GT;
  wp.dataType = WatchDataType::FLOAT32;
  wp.function = WatchFunction::MAGNITUDE;
  wp.magnitudeFields = 3; // 3D vector
  wp.armed = true;

  // Threshold: magnitude > 5.0
  double threshold = 5.0;
  std::memcpy(wp.threshold.data(), &threshold, sizeof(double));

  // Vector (3, 4, 0) -> magnitude = 5.0
  float vec[3] = {3.0F, 4.0F, 0.0F};
  const double MAG = applyWatchFunction(wp, reinterpret_cast<std::uint8_t*>(vec), sizeof(vec));
  EXPECT_NEAR(MAG, 5.0, 0.001);

  // Vector (1, 1, 1) -> magnitude = sqrt(3) = 1.732
  float vec2[3] = {1.0F, 1.0F, 1.0F};
  const double MAG2 = applyWatchFunction(wp, reinterpret_cast<std::uint8_t*>(vec2), sizeof(vec2));
  EXPECT_NEAR(MAG2, std::sqrt(3.0), 0.001);
}

/** @test WatchFunction::MEAN computes rolling average. */
TEST(DataWatchpoint, WatchFunctionMean) {
  DataWatchpoint wp{};
  wp.predicate = WatchPredicate::GT;
  wp.dataType = WatchDataType::FLOAT32;
  wp.function = WatchFunction::MEAN;
  wp.sampleWindow = 4;
  wp.armed = true;

  double threshold = 7.0;
  std::memcpy(wp.threshold.data(), &threshold, sizeof(double));

  // Feed 4 samples: 2, 4, 6, 8 -> mean = 5.0
  float samples[] = {2.0F, 4.0F, 6.0F, 8.0F};
  std::uint8_t bytes[4];
  double result = 0.0;
  for (auto s : samples) {
    std::memcpy(bytes, &s, 4);
    result = applyWatchFunction(wp, bytes, 4);
  }
  EXPECT_NEAR(result, 5.0, 0.001);

  // Feed 4 more: 10, 10, 10, 10 -> mean = 10.0
  for (int i = 0; i < 4; ++i) {
    float v = 10.0F;
    std::memcpy(bytes, &v, 4);
    result = applyWatchFunction(wp, bytes, 4);
  }
  EXPECT_NEAR(result, 10.0, 0.001);
}

/** @test WatchFunction::STALE counts ticks since value changed. */
TEST(DataWatchpoint, WatchFunctionStale) {
  DataWatchpoint wp{};
  wp.predicate = WatchPredicate::GT;
  wp.dataType = WatchDataType::UINT8;
  wp.function = WatchFunction::STALE;
  wp.target.byteLen = 1;
  wp.armed = true;

  // Threshold: stale > 3 ticks
  double threshold = 3.0;
  std::memcpy(wp.threshold.data(), &threshold, sizeof(double));

  std::uint8_t val = 42;

  // First tick: initialize lastValue, staleTicks = 0
  evaluateEdge(wp, &val, 1);
  EXPECT_EQ(wp.staleTicks, 0U);

  // Same value for 3 more ticks
  evaluateEdge(wp, &val, 1);
  evaluateEdge(wp, &val, 1);
  evaluateEdge(wp, &val, 1);
  EXPECT_EQ(wp.staleTicks, 3U);

  // 4th tick: staleTicks = 4 > 3, edge fires
  EXPECT_TRUE(evaluateEdge(wp, &val, 1));
  EXPECT_EQ(wp.staleTicks, 4U);

  // Value changes: staleTicks resets
  std::uint8_t newVal = 99;
  evaluateEdge(wp, &newVal, 1);
  EXPECT_EQ(wp.staleTicks, 0U);
}

/** @test WatchFunction::CUSTOM uses user-supplied compute delegate. */
TEST(DataWatchpoint, WatchFunctionCustomCompute) {
  DataWatchpoint wp{};
  wp.predicate = WatchPredicate::GT;
  wp.dataType = WatchDataType::FLOAT32;
  wp.function = WatchFunction::CUSTOM;
  wp.armed = true;

  // Custom: returns the square of the float value
  auto squareFn = [](void*, const std::uint8_t* data, std::size_t) noexcept -> double {
    float v = 0.0F;
    std::memcpy(&v, data, 4);
    return static_cast<double>(v) * static_cast<double>(v);
  };
  wp.customCompute = {squareFn, nullptr};

  // Threshold: square > 50.0
  double threshold = 50.0;
  std::memcpy(wp.threshold.data(), &threshold, sizeof(double));

  // value = 6.0, square = 36 < 50 -> false
  float v1 = 6.0F;
  std::uint8_t bytes[4];
  std::memcpy(bytes, &v1, 4);
  EXPECT_FALSE(evaluateEdge(wp, bytes, 4));

  // value = 8.0, square = 64 > 50 -> true (edge fire)
  float v2 = 8.0F;
  std::memcpy(bytes, &v2, 4);
  EXPECT_TRUE(evaluateEdge(wp, bytes, 4));
}

/** @test WatchFunction::DELTA with edge detection fires on threshold crossing. */
TEST(DataWatchpoint, WatchFunctionDeltaEdge) {
  DataWatchpoint wp{};
  wp.watchpointId = 1;
  wp.predicate = WatchPredicate::GT;
  wp.dataType = WatchDataType::FLOAT32;
  wp.function = WatchFunction::DELTA;
  wp.eventId = 42;
  wp.armed = true;

  double threshold = 10.0;
  std::memcpy(wp.threshold.data(), &threshold, sizeof(double));

  std::uint8_t bytes[4];

  // Ramp: 0, 5, 8, 25 -> deltas: 0, 5, 3, 17
  float values[] = {0.0F, 5.0F, 8.0F, 25.0F};

  // Prime
  std::memcpy(bytes, &values[0], 4);
  evaluateEdge(wp, bytes, 4);

  // delta=5 < 10
  std::memcpy(bytes, &values[1], 4);
  EXPECT_FALSE(evaluateEdge(wp, bytes, 4));

  // delta=3 < 10
  std::memcpy(bytes, &values[2], 4);
  EXPECT_FALSE(evaluateEdge(wp, bytes, 4));

  // delta=17 > 10 -> edge fire
  std::memcpy(bytes, &values[3], 4);
  EXPECT_TRUE(evaluateEdge(wp, bytes, 4));
  EXPECT_EQ(wp.fireCount, 1U);
}
