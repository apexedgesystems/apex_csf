/**
 * @file Esp32NvsFlash_uTest.cpp
 * @brief Unit tests for Esp32NvsFlash implementation (using mock mode).
 *
 * These tests run on the host by defining APEX_HAL_ESP32_MOCK,
 * which removes ESP-IDF dependencies and provides a simulated
 * 512-byte NVS shadow buffer (1 page of 512 bytes).
 */

#define APEX_HAL_ESP32_MOCK 1

#include "src/system/core/hal/esp32/inc/Esp32NvsFlash.hpp"

#include <gtest/gtest.h>

using apex::hal::FlashGeometry;
using apex::hal::FlashStats;
using apex::hal::FlashStatus;
using apex::hal::esp32::Esp32NvsFlash;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verify Esp32NvsFlash can be default constructed in mock mode. */
TEST(Esp32NvsFlash, DefaultConstruction) {
  Esp32NvsFlash flash;

  EXPECT_FALSE(flash.isInitialized());
  EXPECT_FALSE(flash.isBusy());
}

/* ----------------------------- Init/Deinit Tests ----------------------------- */

/** @test Verify init succeeds in mock mode. */
TEST(Esp32NvsFlash, InitSucceeds) {
  Esp32NvsFlash flash;

  const FlashStatus STATUS = flash.init();

  EXPECT_EQ(STATUS, FlashStatus::OK);
  EXPECT_TRUE(flash.isInitialized());
}

/** @test Verify deinit resets state. */
TEST(Esp32NvsFlash, DeinitResetsState) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  flash.deinit();

  EXPECT_FALSE(flash.isInitialized());
}

/** @test Verify multiple init/deinit cycles work. */
TEST(Esp32NvsFlash, MultipleInitDeinitCycles) {
  Esp32NvsFlash flash;

  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(flash.init(), FlashStatus::OK);
    EXPECT_TRUE(flash.isInitialized());
    flash.deinit();
    EXPECT_FALSE(flash.isInitialized());
  }
}

/** @test Verify double init is handled (re-init without deinit). */
TEST(Esp32NvsFlash, DoubleInitSucceeds) {
  Esp32NvsFlash flash;

  EXPECT_EQ(flash.init(), FlashStatus::OK);
  EXPECT_TRUE(flash.isInitialized());

  // Second init should also succeed
  EXPECT_EQ(flash.init(), FlashStatus::OK);
  EXPECT_TRUE(flash.isInitialized());
}

/* ----------------------------- Read Tests ----------------------------- */

/** @test Verify read returns ERROR_NOT_INIT when not initialized. */
TEST(Esp32NvsFlash, ReadNotInitialized) {
  Esp32NvsFlash flash;
  uint8_t data[4] = {};

  const FlashStatus STATUS = flash.read(0x00000000, data, 4);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_NOT_INIT);
}

/** @test Verify read succeeds and returns erased data (0xFF). */
TEST(Esp32NvsFlash, ReadSucceeds) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  uint8_t data[8] = {};
  const FlashStatus STATUS = flash.read(0x00000000, data, 8);

  EXPECT_EQ(STATUS, FlashStatus::OK);
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(data[i], 0xFF);
  }
}

/** @test Verify read rejects null pointer. */
TEST(Esp32NvsFlash, ReadNullPointer) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  const FlashStatus STATUS = flash.read(0x00000000, nullptr, 4);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify read rejects zero length. */
TEST(Esp32NvsFlash, ReadZeroLength) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  uint8_t data[1] = {};
  const FlashStatus STATUS = flash.read(0x00000000, data, 0);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify read rejects out-of-range address. */
TEST(Esp32NvsFlash, ReadOutOfRange) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  uint8_t data[4] = {};
  const FlashStatus STATUS = flash.read(0x00000000 + 512, data, 4);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify read rejects when length extends past boundary. */
TEST(Esp32NvsFlash, ReadPastBoundary) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  uint8_t data[32] = {};
  // Start at offset 500, try to read 32 bytes (exceeds 512)
  const FlashStatus STATUS = flash.read(0x00000000 + 500, data, 32);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/* ----------------------------- Write Tests ----------------------------- */

/** @test Verify write returns ERROR_NOT_INIT when not initialized. */
TEST(Esp32NvsFlash, WriteNotInitialized) {
  Esp32NvsFlash flash;
  uint8_t data[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

  const FlashStatus STATUS = flash.write(0x00000000, data, 8);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_NOT_INIT);
}

/** @test Verify write succeeds in mock mode. */
TEST(Esp32NvsFlash, WriteSucceeds) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  uint8_t data[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE};
  const FlashStatus STATUS = flash.write(0x00000000, data, 8);

  EXPECT_EQ(STATUS, FlashStatus::OK);
}

/** @test Verify write rejects null pointer. */
TEST(Esp32NvsFlash, WriteNullPointer) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  const FlashStatus STATUS = flash.write(0x00000000, nullptr, 8);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify write rejects zero length. */
TEST(Esp32NvsFlash, WriteZeroLength) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  uint8_t data[8] = {};
  const FlashStatus STATUS = flash.write(0x00000000, data, 0);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify write rejects out-of-range address. */
TEST(Esp32NvsFlash, WriteOutOfRange) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  uint8_t data[8] = {};
  const FlashStatus STATUS = flash.write(0x00000000 + 512, data, 8);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/* ----------------------------- Erase Tests ----------------------------- */

/** @test Verify erase returns ERROR_NOT_INIT when not initialized. */
TEST(Esp32NvsFlash, EraseNotInitialized) {
  Esp32NvsFlash flash;

  const FlashStatus STATUS = flash.erasePage(0);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_NOT_INIT);
}

/** @test Verify single page erase succeeds (only page 0 valid). */
TEST(Esp32NvsFlash, ErasePageSucceeds) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  const FlashStatus STATUS = flash.erasePage(0);

  EXPECT_EQ(STATUS, FlashStatus::OK);
  EXPECT_EQ(flash.stats().pagesErased, 1U);
}

/** @test Verify erase rejects invalid page index (>= 1). */
TEST(Esp32NvsFlash, EraseInvalidPage) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  const FlashStatus STATUS = flash.erasePage(1);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify erasePages works for the single valid range. */
TEST(Esp32NvsFlash, ErasePagesValidRange) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  const FlashStatus STATUS = flash.erasePages(0, 1);

  EXPECT_EQ(STATUS, FlashStatus::OK);
}

/** @test Verify erasePages rejects invalid range. */
TEST(Esp32NvsFlash, ErasePagesInvalidRange) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  EXPECT_EQ(flash.erasePages(0, 2), FlashStatus::ERROR_INVALID_ARG);
  EXPECT_EQ(flash.erasePages(1, 1), FlashStatus::ERROR_INVALID_ARG);
}

/* ----------------------------- Read-After-Write Tests ----------------------------- */

/** @test Verify data can be written and read back. */
TEST(Esp32NvsFlash, WriteAndReadBack) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  uint8_t tx[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
  uint8_t rx[16] = {};

  EXPECT_EQ(flash.write(0x00000000, tx, 16), FlashStatus::OK);
  EXPECT_EQ(flash.read(0x00000000, rx, 16), FlashStatus::OK);

  for (int i = 0; i < 16; ++i) {
    EXPECT_EQ(rx[i], tx[i]);
  }
}

/** @test Verify erase resets data to 0xFF. */
TEST(Esp32NvsFlash, EraseResetsToFF) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  uint8_t tx[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};
  static_cast<void>(flash.write(0x00000000, tx, 8));

  EXPECT_EQ(flash.erasePage(0), FlashStatus::OK);

  uint8_t rx[8] = {};
  static_cast<void>(flash.read(0x00000000, rx, 8));

  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(rx[i], 0xFF);
  }
}

/** @test Verify write at non-zero offset and read back. */
TEST(Esp32NvsFlash, WriteAtOffset) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  uint8_t tx[4] = {0xDE, 0xAD, 0xBE, 0xEF};
  EXPECT_EQ(flash.write(0x00000000 + 256, tx, 4), FlashStatus::OK);

  uint8_t rx[4] = {};
  EXPECT_EQ(flash.read(0x00000000 + 256, rx, 4), FlashStatus::OK);

  EXPECT_EQ(rx[0], 0xDE);
  EXPECT_EQ(rx[1], 0xAD);
  EXPECT_EQ(rx[2], 0xBE);
  EXPECT_EQ(rx[3], 0xEF);
}

/** @test Verify boundary write at last valid bytes. */
TEST(Esp32NvsFlash, WriteBoundary) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  uint8_t tx[4] = {0xCA, 0xFE, 0xBA, 0xBE};
  // Write to last 4 bytes of the 512-byte region
  EXPECT_EQ(flash.write(0x00000000 + 508, tx, 4), FlashStatus::OK);

  uint8_t rx[4] = {};
  EXPECT_EQ(flash.read(0x00000000 + 508, rx, 4), FlashStatus::OK);

  EXPECT_EQ(rx[0], 0xCA);
  EXPECT_EQ(rx[1], 0xFE);
  EXPECT_EQ(rx[2], 0xBA);
  EXPECT_EQ(rx[3], 0xBE);
}

/* ----------------------------- Geometry Tests ----------------------------- */

/** @test Verify geometry reports correct values for NVS-backed flash. */
TEST(Esp32NvsFlash, ReportsValidGeometry) {
  Esp32NvsFlash flash;

  const auto GEO = flash.geometry();

  EXPECT_EQ(GEO.baseAddress, 0x00000000U);
  EXPECT_EQ(GEO.totalSize, 512U);
  EXPECT_EQ(GEO.pageSize, 512U);
  EXPECT_EQ(GEO.writeAlignment, 1U);
  EXPECT_EQ(GEO.pageCount, 1U);
  EXPECT_EQ(GEO.bankCount, 1U);
}

/** @test Verify pageForAddress always returns 0 (single page). */
TEST(Esp32NvsFlash, PageForAddress) {
  Esp32NvsFlash flash;

  EXPECT_EQ(flash.pageForAddress(0x00000000), 0U);
  EXPECT_EQ(flash.pageForAddress(0x000000FF), 0U);
}

/** @test Verify addressForPage returns base address. */
TEST(Esp32NvsFlash, AddressForPage) {
  Esp32NvsFlash flash;

  EXPECT_EQ(flash.addressForPage(0), 0x00000000U);
}

/* ----------------------------- Stats Tests ----------------------------- */

/** @test Verify initial stats are zero. */
TEST(Esp32NvsFlash, InitialStatsZero) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  const auto& STATS = flash.stats();

  EXPECT_EQ(STATS.bytesWritten, 0U);
  EXPECT_EQ(STATS.bytesRead, 0U);
  EXPECT_EQ(STATS.pagesErased, 0U);
  EXPECT_EQ(STATS.totalErrors(), 0U);
}

/** @test Verify write increments stats. */
TEST(Esp32NvsFlash, WriteIncrementsStats) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  uint8_t data[16] = {};
  static_cast<void>(flash.write(0x00000000, data, 16));

  EXPECT_EQ(flash.stats().bytesWritten, 16U);
}

/** @test Verify read increments stats. */
TEST(Esp32NvsFlash, ReadIncrementsStats) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  uint8_t data[32] = {};
  static_cast<void>(flash.read(0x00000000, data, 32));

  EXPECT_EQ(flash.stats().bytesRead, 32U);
}

/** @test Verify erase increments stats. */
TEST(Esp32NvsFlash, EraseIncrementsStats) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  static_cast<void>(flash.erasePage(0));

  EXPECT_EQ(flash.stats().pagesErased, 1U);
}

/** @test Verify failed operations do not increment stats. */
TEST(Esp32NvsFlash, FailedOpsNoStats) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  static_cast<void>(flash.write(0x00000000, nullptr, 8));
  static_cast<void>(flash.read(0x00000000, nullptr, 8));

  EXPECT_EQ(flash.stats().bytesWritten, 0U);
  EXPECT_EQ(flash.stats().bytesRead, 0U);
}

/** @test Verify resetStats clears counters. */
TEST(Esp32NvsFlash, ResetStats) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  uint8_t data[8] = {};
  static_cast<void>(flash.write(0x00000000, data, 8));
  static_cast<void>(flash.read(0x00000000, data, 8));
  static_cast<void>(flash.erasePage(0));

  EXPECT_EQ(flash.stats().bytesWritten, 8U);

  flash.resetStats();

  EXPECT_EQ(flash.stats().bytesWritten, 0U);
  EXPECT_EQ(flash.stats().bytesRead, 0U);
  EXPECT_EQ(flash.stats().pagesErased, 0U);
}

/* ----------------------------- isBusy Tests ----------------------------- */

/** @test Verify isBusy returns false when not initialized. */
TEST(Esp32NvsFlash, BusyNotInit) {
  Esp32NvsFlash flash;
  EXPECT_FALSE(flash.isBusy());
}

/** @test Verify isBusy returns false in mock mode. */
TEST(Esp32NvsFlash, BusyAfterInit) {
  Esp32NvsFlash flash;
  static_cast<void>(flash.init());

  EXPECT_FALSE(flash.isBusy());
}
