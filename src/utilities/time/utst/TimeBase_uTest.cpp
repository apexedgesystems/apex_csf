/**
 * @file TimeBase_uTest.cpp
 * @brief Unit tests for time utilities (TimeBase, SystemClocks, TimeConvert).
 */

#include "src/utilities/time/inc/TimeBase.hpp"
#include "src/utilities/time/inc/SystemClocks.hpp"
#include "src/utilities/time/inc/TimeConvert.hpp"

#include <gtest/gtest.h>

#include <cmath>

using apex::time::GPS_EPOCH_UNIX_SECONDS;
using apex::time::GPS_TAI_OFFSET_SECONDS;
using apex::time::GpsContext;
using apex::time::MetContext;
using apex::time::TAI_UTC_OFFSET_SECONDS;
using apex::time::TaiContext;
using apex::time::TimeProviderDelegate;
using apex::time::Timestamp;
using apex::time::TimeStandard;

/* ----------------------------- Enum Tests ----------------------------- */

/** @test TimeStandard toString covers all values. */
TEST(TimeBase, TimeStandardToString) {
  EXPECT_STREQ(apex::time::toString(TimeStandard::CYCLE), "CYCLE");
  EXPECT_STREQ(apex::time::toString(TimeStandard::MONOTONIC), "MONOTONIC");
  EXPECT_STREQ(apex::time::toString(TimeStandard::UTC), "UTC");
  EXPECT_STREQ(apex::time::toString(TimeStandard::TAI), "TAI");
  EXPECT_STREQ(apex::time::toString(TimeStandard::GPS), "GPS");
  EXPECT_STREQ(apex::time::toString(TimeStandard::MET), "MET");
}

/* ----------------------------- Constants ----------------------------- */

/** @test TAI-UTC offset is 37 seconds (current as of 2017). */
TEST(TimeBase, TaiUtcOffset) { EXPECT_EQ(TAI_UTC_OFFSET_SECONDS, 37); }

/** @test GPS epoch is 1980-01-06 in Unix seconds. */
TEST(TimeBase, GpsEpoch) { EXPECT_EQ(GPS_EPOCH_UNIX_SECONDS, 315964800); }

/** @test GPS-TAI offset is 19 seconds. */
TEST(TimeBase, GpsTaiOffset) { EXPECT_EQ(GPS_TAI_OFFSET_SECONDS, 19); }

/* ----------------------------- Timestamp ----------------------------- */

/** @test Default Timestamp is zero CYCLE. */
TEST(TimeBase, TimestampDefault) {
  Timestamp ts{};
  EXPECT_EQ(ts.microseconds, 0U);
  EXPECT_EQ(ts.standard, TimeStandard::CYCLE);
}

/** @test Timestamps with same standard compare correctly. */
TEST(TimeBase, TimestampComparison) {
  Timestamp a{100, TimeStandard::UTC};
  Timestamp b{200, TimeStandard::UTC};
  Timestamp c{100, TimeStandard::UTC};

  EXPECT_TRUE(a < b);
  EXPECT_FALSE(b < a);
  EXPECT_EQ(a, c);
  EXPECT_NE(a, b);
}

/** @test Timestamps with different standards are not equal. */
TEST(TimeBase, TimestampDifferentStandard) {
  Timestamp utc{100, TimeStandard::UTC};
  Timestamp tai{100, TimeStandard::TAI};

  EXPECT_NE(utc, tai);
  EXPECT_FALSE(utc < tai); // Different standards, comparison is false
}

/* ----------------------------- TimeProviderDelegate ----------------------------- */

/** @test TimeProviderDelegate can wrap a static function. */
TEST(TimeBase, DelegateWrapsFunction) {
  TimeProviderDelegate provider{apex::time::monotonicMicroseconds, nullptr};
  EXPECT_TRUE(static_cast<bool>(provider));

  const std::uint64_t T1 = provider();
  const std::uint64_t T2 = provider();
  EXPECT_GE(T2, T1); // Monotonic
  EXPECT_GT(T1, 0U); // System has been up for some time
}

/* ----------------------------- System Clocks ----------------------------- */

/** @test Monotonic clock returns increasing values. */
TEST(SystemClocks, MonotonicIncreasing) {
  const std::uint64_t T1 = apex::time::monotonicMicroseconds(nullptr);
  const std::uint64_t T2 = apex::time::monotonicMicroseconds(nullptr);
  EXPECT_GE(T2, T1);
}

/** @test UTC clock returns plausible value (after 2020). */
TEST(SystemClocks, UtcPlausible) {
  const std::uint64_t NOW = apex::time::utcMicroseconds(nullptr);
  // 2020-01-01 = 1577836800 seconds since epoch
  const std::uint64_t MIN_2020 = 1577836800ULL * 1000000ULL;
  EXPECT_GT(NOW, MIN_2020);
}

/** @test TAI clock is UTC + offset. */
TEST(SystemClocks, TaiGreaterThanUtc) {
  TaiContext ctx{37};
  const std::uint64_t UTC_NOW = apex::time::utcMicroseconds(nullptr);
  const std::uint64_t TAI_NOW = apex::time::taiMicroseconds(&ctx);

  // TAI should be ~37 seconds ahead of UTC (within tolerance for read gap)
  const std::int64_t DIFF = static_cast<std::int64_t>(TAI_NOW) - static_cast<std::int64_t>(UTC_NOW);
  const std::int64_t EXPECTED = 37LL * 1000000LL;
  EXPECT_NEAR(DIFF, EXPECTED, 100000); // Within 100ms tolerance
}

/** @test TAI with null context uses default offset. */
TEST(SystemClocks, TaiNullContext) {
  const std::uint64_t TAI = apex::time::taiMicroseconds(nullptr);
  EXPECT_GT(TAI, 0U);
}

/** @test GPS clock returns plausible value. */
TEST(SystemClocks, GpsPlausible) {
  GpsContext ctx{37};
  const std::uint64_t GPS = apex::time::gpsMicroseconds(&ctx);
  EXPECT_GT(GPS, 0U);

  // GPS time should be less than UTC (different epoch, further in past)
  const std::uint64_t UTC_NOW = apex::time::utcMicroseconds(nullptr);
  EXPECT_LT(GPS, UTC_NOW);
}

/** @test MET returns time since epoch. */
TEST(SystemClocks, MetElapsedTime) {
  const std::uint64_t NOW = apex::time::monotonicMicroseconds(nullptr);
  MetContext ctx{NOW}; // Epoch = now

  // MET should be near zero (just started)
  const std::uint64_t MET = apex::time::metMicroseconds(&ctx);
  EXPECT_LT(MET, 1000000U); // Less than 1 second
}

/** @test MET with epoch in the past returns positive elapsed time. */
TEST(SystemClocks, MetPositiveElapsed) {
  MetContext ctx{0}; // Epoch at boot
  const std::uint64_t MET = apex::time::metMicroseconds(&ctx);
  EXPECT_GT(MET, 0U); // System has been up for some time
}

/* ----------------------------- Time Conversions ----------------------------- */

/** @test secondsToMicroseconds basic conversion. */
TEST(TimeConvert, SecondsToMicroseconds) {
  EXPECT_EQ(apex::time::secondsToMicroseconds(1.0), 1000000U);
  EXPECT_EQ(apex::time::secondsToMicroseconds(0.5), 500000U);
  EXPECT_EQ(apex::time::secondsToMicroseconds(0.0), 0U);
  EXPECT_EQ(apex::time::secondsToMicroseconds(-1.0), 0U);
}

/** @test microsecondsToSeconds round-trip. */
TEST(TimeConvert, MicrosecondsToSeconds) {
  EXPECT_DOUBLE_EQ(apex::time::microsecondsToSeconds(1000000), 1.0);
  EXPECT_DOUBLE_EQ(apex::time::microsecondsToSeconds(500000), 0.5);
}

/** @test secondsToCycles at 100 Hz. */
TEST(TimeConvert, SecondsToCycles) {
  EXPECT_EQ(apex::time::secondsToCycles(1.0, 100), 100U);
  EXPECT_EQ(apex::time::secondsToCycles(5.0, 100), 500U);
  EXPECT_EQ(apex::time::secondsToCycles(0.01, 100), 1U);
  EXPECT_EQ(apex::time::secondsToCycles(1.0, 0), 0U); // Zero freq
}

/** @test cyclesToMicroseconds at 100 Hz. */
TEST(TimeConvert, CyclesToMicroseconds) {
  EXPECT_EQ(apex::time::cyclesToMicroseconds(100, 100), 1000000U);
  EXPECT_EQ(apex::time::cyclesToMicroseconds(1, 100), 10000U);
  EXPECT_EQ(apex::time::cyclesToMicroseconds(100, 0), 0U); // Zero freq
}

/** @test microsecondsToCycles at 100 Hz. */
TEST(TimeConvert, MicrosecondsToCycles) {
  EXPECT_EQ(apex::time::microsecondsToCycles(1000000, 100), 100U);
  EXPECT_EQ(apex::time::microsecondsToCycles(10000, 100), 1U);
}

/** @test UTC to TAI adds offset. */
TEST(TimeConvert, UtcToTai) {
  const std::uint64_t UTC = 1000000000ULL; // 1 second
  const std::uint64_t TAI = apex::time::utcToTai(UTC, 37);
  EXPECT_EQ(TAI, UTC + 37ULL * 1000000ULL);
}

/** @test TAI to UTC subtracts offset. */
TEST(TimeConvert, TaiToUtc) {
  const std::uint64_t TAI = 1000000000ULL + 37ULL * 1000000ULL;
  const std::uint64_t UTC = apex::time::taiToUtc(TAI, 37);
  EXPECT_EQ(UTC, 1000000000ULL);
}

/** @test UTC->TAI->UTC round-trip. */
TEST(TimeConvert, UtcTaiRoundTrip) {
  const std::uint64_t ORIGINAL = 1700000000ULL * 1000000ULL;
  const std::uint64_t TAI = apex::time::utcToTai(ORIGINAL);
  const std::uint64_t BACK = apex::time::taiToUtc(TAI);
  EXPECT_EQ(BACK, ORIGINAL);
}

/** @test TAI to GPS conversion. */
TEST(TimeConvert, TaiToGps) {
  // TAI at GPS epoch + 19 seconds should give GPS time = 0
  const std::uint64_t TAI_AT_GPS_EPOCH =
      (static_cast<std::uint64_t>(GPS_EPOCH_UNIX_SECONDS) + GPS_TAI_OFFSET_SECONDS) * 1000000ULL;
  EXPECT_EQ(apex::time::taiToGps(TAI_AT_GPS_EPOCH), 0U);

  // 1 second after GPS epoch
  const std::uint64_t TAI_PLUS_1 = TAI_AT_GPS_EPOCH + 1000000ULL;
  EXPECT_EQ(apex::time::taiToGps(TAI_PLUS_1), 1000000U);
}

/** @test GPS->TAI->GPS round-trip. */
TEST(TimeConvert, GpsTaiRoundTrip) {
  const std::uint64_t GPS_TIME = 1000000000ULL; // 1000 seconds in GPS
  const std::uint64_t TAI = apex::time::gpsToTai(GPS_TIME);
  const std::uint64_t BACK = apex::time::taiToGps(TAI);
  EXPECT_EQ(BACK, GPS_TIME);
}

/** @test UTC to GPS convenience function. */
TEST(TimeConvert, UtcToGps) {
  const std::uint64_t UTC = 1700000000ULL * 1000000ULL; // ~2023
  const std::uint64_t GPS_DIRECT = apex::time::utcToGps(UTC);
  const std::uint64_t GPS_VIA_TAI = apex::time::taiToGps(apex::time::utcToTai(UTC));
  EXPECT_EQ(GPS_DIRECT, GPS_VIA_TAI);
}
