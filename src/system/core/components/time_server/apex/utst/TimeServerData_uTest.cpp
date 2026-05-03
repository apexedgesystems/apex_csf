/**
 * @file TimeServerData_uTest.cpp
 * @brief Unit tests for TimeServer data types (sizes, defaults, toString).
 *
 * Verifies the wire-format invariants that must hold across compilers,
 * since TimeAtNextTone, SetReferenceTime, SetTimeManual,
 * TimeServerTunableParams, and TimeServerOutput are inter-component
 * contracts. Logic tests live with the TimeServer component itself.
 */

#include "src/system/core/components/time_server/apex/inc/TimeServerData.hpp"

#include <gtest/gtest.h>

#include <cstring>

using system_core::time_server::SetReferenceTime;
using system_core::time_server::SetTimeManual;
using system_core::time_server::TimeAtNextTone;
using system_core::time_server::TimeQuality;
using system_core::time_server::TimeServerMode;
using system_core::time_server::TimeServerOutput;
using system_core::time_server::TimeServerTunableParams;
using system_core::time_server::TimeSource;
using system_core::time_server::TimeValid;
using system_core::time_server::toString;
using system_core::time_server::TNT_FLAG_LEAP_SECOND_PENDING;
using system_core::time_server::TNT_FLAG_REF_SWITCHOVER;

/* ----------------------------- Wire layout ----------------------------- */

/** @test TNT is exactly 40 bytes (verified by static_assert; runtime echoes). */
TEST(TimeServerData, TntIs40Bytes) { EXPECT_EQ(sizeof(TimeAtNextTone), 40U); }

/** @test SetReferenceTime is 16 bytes. */
TEST(TimeServerData, SetReferenceTimeIs16Bytes) {
  EXPECT_EQ(sizeof(SetReferenceTime), 16U);
}

/** @test SetTimeManual is 8 bytes. */
TEST(TimeServerData, SetTimeManualIs8Bytes) { EXPECT_EQ(sizeof(SetTimeManual), 8U); }

/** @test TimeServerTunableParams is 16 bytes. */
TEST(TimeServerData, TunableParamsIs16Bytes) {
  EXPECT_EQ(sizeof(TimeServerTunableParams), 16U);
}

/** @test TimeServerOutput is 56 bytes. */
TEST(TimeServerData, OutputIs56Bytes) { EXPECT_EQ(sizeof(TimeServerOutput), 56U); }

/* ----------------------------- Default-construction ----------------------------- */

/** @test TNT default-constructs to zeros. */
TEST(TimeServerData, TntDefaultZero) {
  TimeAtNextTone tnt{};
  EXPECT_EQ(tnt.epochNs, 0);
  EXPECT_EQ(tnt.localNs, 0);
  EXPECT_EQ(tnt.nextToneEpochNs, 0);
  EXPECT_EQ(tnt.driftPpb, 0);
  EXPECT_EQ(tnt.ppsCount, 0U);
  EXPECT_EQ(tnt.source, 0);
  EXPECT_EQ(tnt.quality, 0);
  EXPECT_EQ(tnt.valid, 0);
  EXPECT_EQ(tnt.flags, 0);
}

/** @test TPRM defaults match documented expectations. */
TEST(TimeServerData, TunableParamsDefaults) {
  TimeServerTunableParams tprm;
  EXPECT_EQ(tprm.mode, static_cast<std::uint8_t>(TimeServerMode::PRIMARY));
  EXPECT_EQ(tprm.ppsDeviceIndex, 0);
  EXPECT_EQ(tprm.primaryRefSource, static_cast<std::uint8_t>(TimeSource::GPS));
  EXPECT_EQ(tprm.maxStalenessUs, 1'500'000U);
  EXPECT_EQ(tprm.driftFilterTaps, 16U);
  EXPECT_EQ(tprm.holdoverLimitS, 60U);
}

/* ----------------------------- Enum encoding ----------------------------- */

/** @test TimeSource encodes per the design doc. */
TEST(TimeServerData, TimeSourceValues) {
  EXPECT_EQ(static_cast<int>(TimeSource::GPS), 0);
  EXPECT_EQ(static_cast<int>(TimeSource::GROUND), 1);
  EXPECT_EQ(static_cast<int>(TimeSource::ONBOARD), 2);
  EXPECT_EQ(static_cast<int>(TimeSource::MANUAL), 3);
  EXPECT_EQ(static_cast<int>(TimeSource::SIM), 4);
}

/** @test TimeQuality encodes per the design doc. */
TEST(TimeServerData, TimeQualityValues) {
  EXPECT_EQ(static_cast<int>(TimeQuality::UNKNOWN), 0);
  EXPECT_EQ(static_cast<int>(TimeQuality::COARSE), 1);
  EXPECT_EQ(static_cast<int>(TimeQuality::FINE), 2);
  EXPECT_EQ(static_cast<int>(TimeQuality::PRECISE), 3);
}

/** @test TimeValid encodes per the design doc. */
TEST(TimeServerData, TimeValidValues) {
  EXPECT_EQ(static_cast<int>(TimeValid::NONE), 0);
  EXPECT_EQ(static_cast<int>(TimeValid::VALID), 1);
  EXPECT_EQ(static_cast<int>(TimeValid::STALE), 2);
  EXPECT_EQ(static_cast<int>(TimeValid::FREERUN), 3);
}

/** @test TimeServerMode encodes per the design doc. */
TEST(TimeServerData, TimeServerModeValues) {
  EXPECT_EQ(static_cast<int>(TimeServerMode::PRIMARY), 0);
  EXPECT_EQ(static_cast<int>(TimeServerMode::SECONDARY), 1);
  EXPECT_EQ(static_cast<int>(TimeServerMode::PTP_SYNC), 2);
  EXPECT_EQ(static_cast<int>(TimeServerMode::CAN_SYNC), 3);
  EXPECT_EQ(static_cast<int>(TimeServerMode::RELAY), 4);
}

/* ----------------------------- toString ----------------------------- */

/** @test toString returns non-null for every TimeSource value. */
TEST(TimeServerData, ToStringTimeSourceComplete) {
  EXPECT_STREQ(toString(TimeSource::GPS), "GPS");
  EXPECT_STREQ(toString(TimeSource::GROUND), "GROUND");
  EXPECT_STREQ(toString(TimeSource::ONBOARD), "ONBOARD");
  EXPECT_STREQ(toString(TimeSource::MANUAL), "MANUAL");
  EXPECT_STREQ(toString(TimeSource::SIM), "SIM");
}

/** @test toString returns non-null for every TimeQuality value. */
TEST(TimeServerData, ToStringTimeQualityComplete) {
  EXPECT_STREQ(toString(TimeQuality::UNKNOWN), "UNKNOWN");
  EXPECT_STREQ(toString(TimeQuality::COARSE), "COARSE");
  EXPECT_STREQ(toString(TimeQuality::FINE), "FINE");
  EXPECT_STREQ(toString(TimeQuality::PRECISE), "PRECISE");
}

/** @test toString returns non-null for every TimeValid value. */
TEST(TimeServerData, ToStringTimeValidComplete) {
  EXPECT_STREQ(toString(TimeValid::NONE), "NONE");
  EXPECT_STREQ(toString(TimeValid::VALID), "VALID");
  EXPECT_STREQ(toString(TimeValid::STALE), "STALE");
  EXPECT_STREQ(toString(TimeValid::FREERUN), "FREERUN");
}

/** @test toString returns non-null for every TimeServerMode value. */
TEST(TimeServerData, ToStringTimeServerModeComplete) {
  EXPECT_STREQ(toString(TimeServerMode::PRIMARY), "PRIMARY");
  EXPECT_STREQ(toString(TimeServerMode::SECONDARY), "SECONDARY");
  EXPECT_STREQ(toString(TimeServerMode::PTP_SYNC), "PTP_SYNC");
  EXPECT_STREQ(toString(TimeServerMode::CAN_SYNC), "CAN_SYNC");
  EXPECT_STREQ(toString(TimeServerMode::RELAY), "RELAY");
}

/** @test toString handles unknown enum values without crashing. */
TEST(TimeServerData, ToStringUnknown) {
  EXPECT_NE(toString(static_cast<TimeSource>(255)), nullptr);
  EXPECT_NE(toString(static_cast<TimeQuality>(255)), nullptr);
  EXPECT_NE(toString(static_cast<TimeValid>(255)), nullptr);
  EXPECT_NE(toString(static_cast<TimeServerMode>(255)), nullptr);
}

/* ----------------------------- Flag bits ----------------------------- */

/** @test TNT flag bits are distinct and at expected positions. */
TEST(TimeServerData, TntFlagBits) {
  EXPECT_EQ(TNT_FLAG_LEAP_SECOND_PENDING, 0x01);
  EXPECT_EQ(TNT_FLAG_REF_SWITCHOVER, 0x02);
  EXPECT_EQ(TNT_FLAG_LEAP_SECOND_PENDING & TNT_FLAG_REF_SWITCHOVER, 0);
}

/** @test TNT flags can be OR-combined and tested. */
TEST(TimeServerData, TntFlagComposition) {
  TimeAtNextTone tnt{};
  tnt.flags = TNT_FLAG_LEAP_SECOND_PENDING | TNT_FLAG_REF_SWITCHOVER;
  EXPECT_TRUE(tnt.flags & TNT_FLAG_LEAP_SECOND_PENDING);
  EXPECT_TRUE(tnt.flags & TNT_FLAG_REF_SWITCHOVER);
}

/* ----------------------------- Round-trip via memcpy ----------------------------- */

/** @test TNT survives byte-wise serialization. */
TEST(TimeServerData, TntMemcpyRoundtrip) {
  TimeAtNextTone src{};
  src.epochNs = 1'700'000'000'000'000'000LL;
  src.localNs = 12'345'678'901'234LL;
  src.nextToneEpochNs = src.epochNs + 1'000'000'000LL;
  src.driftPpb = -42;
  src.ppsCount = 999;
  src.source = static_cast<std::uint8_t>(TimeSource::GPS);
  src.quality = static_cast<std::uint8_t>(TimeQuality::PRECISE);
  src.valid = static_cast<std::uint8_t>(TimeValid::VALID);
  src.flags = TNT_FLAG_LEAP_SECOND_PENDING;

  std::uint8_t buffer[sizeof(TimeAtNextTone)];
  std::memcpy(buffer, &src, sizeof(buffer));

  TimeAtNextTone dst{};
  std::memcpy(&dst, buffer, sizeof(buffer));

  EXPECT_EQ(dst.epochNs, src.epochNs);
  EXPECT_EQ(dst.localNs, src.localNs);
  EXPECT_EQ(dst.nextToneEpochNs, src.nextToneEpochNs);
  EXPECT_EQ(dst.driftPpb, src.driftPpb);
  EXPECT_EQ(dst.ppsCount, src.ppsCount);
  EXPECT_EQ(dst.source, src.source);
  EXPECT_EQ(dst.quality, src.quality);
  EXPECT_EQ(dst.valid, src.valid);
  EXPECT_EQ(dst.flags, src.flags);
}
