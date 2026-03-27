/**
 * @file EventNotification_uTest.cpp
 * @brief Unit tests for EventNotification struct and helper functions.
 */

#include "src/system/core/infrastructure/data/inc/EventNotification.hpp"

#include <gtest/gtest.h>

using system_core::data::dispatchEvent;
using system_core::data::EVENT_NOTIFICATION_TABLE_SIZE;
using system_core::data::EventNotification;
using system_core::data::EventNotifyDelegate;
using system_core::data::invokeNotification;
using system_core::data::shouldNotify;

/* ----------------------------- Constants ----------------------------- */

/** @test EVENT_NOTIFICATION_TABLE_SIZE is 8. */
TEST(EventNotification, TableSizeConstant) { EXPECT_EQ(EVENT_NOTIFICATION_TABLE_SIZE, 8U); }

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default-constructed EventNotification has expected defaults. */
TEST(EventNotification, DefaultConstruction) {
  const EventNotification NOTE{};
  EXPECT_EQ(NOTE.eventId, 0U);
  EXPECT_FALSE(NOTE.armed);
  EXPECT_EQ(NOTE.invokeCount, 0U);
  EXPECT_FALSE(NOTE.callback);
}

/* ----------------------------- shouldNotify ----------------------------- */

/** @test shouldNotify returns false when not armed. */
TEST(EventNotification, ShouldNotifyDisarmed) {
  std::uint32_t counter = 0;
  auto fn = [](void* ctx, std::uint16_t, std::uint32_t) noexcept {
    ++(*static_cast<std::uint32_t*>(ctx));
  };

  EventNotification note{};
  note.eventId = 1;
  note.callback = {fn, &counter};
  note.armed = false;

  EXPECT_FALSE(shouldNotify(note, 1));
}

/** @test shouldNotify returns false for mismatched eventId. */
TEST(EventNotification, ShouldNotifyWrongEvent) {
  std::uint32_t counter = 0;
  auto fn = [](void* ctx, std::uint16_t, std::uint32_t) noexcept {
    ++(*static_cast<std::uint32_t*>(ctx));
  };

  EventNotification note{};
  note.eventId = 1;
  note.callback = {fn, &counter};
  note.armed = true;

  EXPECT_FALSE(shouldNotify(note, 2));
}

/** @test shouldNotify returns false when callback is empty. */
TEST(EventNotification, ShouldNotifyNoCallback) {
  EventNotification note{};
  note.eventId = 1;
  note.armed = true;

  EXPECT_FALSE(shouldNotify(note, 1));
}

/** @test shouldNotify returns true when armed, matching, and has callback. */
TEST(EventNotification, ShouldNotifyMatch) {
  std::uint32_t counter = 0;
  auto fn = [](void* ctx, std::uint16_t, std::uint32_t) noexcept {
    ++(*static_cast<std::uint32_t*>(ctx));
  };

  EventNotification note{};
  note.eventId = 5;
  note.callback = {fn, &counter};
  note.armed = true;

  EXPECT_TRUE(shouldNotify(note, 5));
}

/* ----------------------------- invokeNotification ----------------------------- */

/** @test invokeNotification calls callback and increments invokeCount. */
TEST(EventNotification, InvokeNotification) {
  std::uint32_t counter = 0;
  auto fn = [](void* ctx, std::uint16_t, std::uint32_t) noexcept {
    ++(*static_cast<std::uint32_t*>(ctx));
  };

  EventNotification note{};
  note.eventId = 3;
  note.callback = {fn, &counter};
  note.armed = true;

  invokeNotification(note, 3, 1);
  EXPECT_EQ(counter, 1U);
  EXPECT_EQ(note.invokeCount, 1U);

  invokeNotification(note, 3, 2);
  EXPECT_EQ(counter, 2U);
  EXPECT_EQ(note.invokeCount, 2U);
}

/** @test invokeNotification passes correct eventId and fireCount. */
TEST(EventNotification, InvokePassesArgs) {
  struct Ctx {
    std::uint16_t lastEvent{0};
    std::uint32_t lastCount{0};
  };
  Ctx ctx{};

  auto fn = [](void* c, std::uint16_t eid, std::uint32_t fc) noexcept {
    auto* p = static_cast<Ctx*>(c);
    p->lastEvent = eid;
    p->lastCount = fc;
  };

  EventNotification note{};
  note.eventId = 42;
  note.callback = {fn, &ctx};
  note.armed = true;

  invokeNotification(note, 42, 7);
  EXPECT_EQ(ctx.lastEvent, 42U);
  EXPECT_EQ(ctx.lastCount, 7U);
}

/* ----------------------------- dispatchEvent ----------------------------- */

/** @test dispatchEvent with null table returns 0. */
TEST(EventNotification, DispatchNullTable) { EXPECT_EQ(dispatchEvent(nullptr, 4, 1, 1), 0U); }

/** @test dispatchEvent invokes matching notifications. */
TEST(EventNotification, DispatchMatchingEntries) {
  std::uint32_t counterA = 0;
  std::uint32_t counterB = 0;
  auto fn = [](void* ctx, std::uint16_t, std::uint32_t) noexcept {
    ++(*static_cast<std::uint32_t*>(ctx));
  };

  std::array<EventNotification, 4> table{};

  // Entry 0: armed, eventId=1
  table[0].eventId = 1;
  table[0].callback = {fn, &counterA};
  table[0].armed = true;

  // Entry 1: armed, eventId=2 (no match)
  table[1].eventId = 2;
  table[1].callback = {fn, &counterB};
  table[1].armed = true;

  // Entry 2: armed, eventId=1 (matches)
  table[2].eventId = 1;
  table[2].callback = {fn, &counterB};
  table[2].armed = true;

  // Entry 3: not armed, eventId=1 (no match - disarmed)
  table[3].eventId = 1;
  table[3].callback = {fn, &counterA};
  table[3].armed = false;

  const std::uint8_t COUNT = dispatchEvent(table.data(), table.size(), 1, 10);
  EXPECT_EQ(COUNT, 2U);
  EXPECT_EQ(counterA, 1U);
  EXPECT_EQ(counterB, 1U);
}

/** @test dispatchEvent returns 0 when no entries match. */
TEST(EventNotification, DispatchNoMatch) {
  std::uint32_t counter = 0;
  auto fn = [](void* ctx, std::uint16_t, std::uint32_t) noexcept {
    ++(*static_cast<std::uint32_t*>(ctx));
  };

  std::array<EventNotification, 2> table{};
  table[0].eventId = 5;
  table[0].callback = {fn, &counter};
  table[0].armed = true;

  table[1].eventId = 6;
  table[1].callback = {fn, &counter};
  table[1].armed = true;

  const std::uint8_t COUNT = dispatchEvent(table.data(), table.size(), 99, 1);
  EXPECT_EQ(COUNT, 0U);
  EXPECT_EQ(counter, 0U);
}

/** @test dispatchEvent updates invokeCount on each notification. */
TEST(EventNotification, DispatchUpdatesInvokeCount) {
  std::uint32_t counter = 0;
  auto fn = [](void* ctx, std::uint16_t, std::uint32_t) noexcept {
    ++(*static_cast<std::uint32_t*>(ctx));
  };

  std::array<EventNotification, 1> table{};
  table[0].eventId = 10;
  table[0].callback = {fn, &counter};
  table[0].armed = true;

  dispatchEvent(table.data(), table.size(), 10, 1);
  EXPECT_EQ(table[0].invokeCount, 1U);

  dispatchEvent(table.data(), table.size(), 10, 2);
  EXPECT_EQ(table[0].invokeCount, 2U);
}
