/**
 * @file Stm32Flash_uTest.cpp
 * @brief Unit tests for Stm32Flash implementation (using mock mode).
 *
 * These tests run on the host by defining APEX_HAL_STM32_MOCK,
 * which removes STM32 HAL dependencies and provides a simulated
 * 64KB flash region (32 pages of 2KB).
 */

#define APEX_HAL_STM32_MOCK 1

#include "src/system/core/hal/stm32/inc/Stm32Flash.hpp"

#include <gtest/gtest.h>

using apex::hal::FlashGeometry;
using apex::hal::FlashStats;
using apex::hal::FlashStatus;
using apex::hal::stm32::Stm32Flash;
using apex::hal::stm32::Stm32FlashOptions;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verify Stm32Flash can be default constructed in mock mode. */
TEST(Stm32Flash, DefaultConstruction) {
  Stm32Flash flash;

  EXPECT_FALSE(flash.isInitialized());
  EXPECT_FALSE(flash.isBusy());
}

/* ----------------------------- Init/Deinit Tests ----------------------------- */

/** @test Verify init succeeds in mock mode. */
TEST(Stm32Flash, InitSucceeds) {
  Stm32Flash flash;

  const FlashStatus STATUS = flash.init();

  EXPECT_EQ(STATUS, FlashStatus::OK);
  EXPECT_TRUE(flash.isInitialized());
}

/** @test Verify deinit resets state. */
TEST(Stm32Flash, DeinitResetsState) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  flash.deinit();

  EXPECT_FALSE(flash.isInitialized());
}

/** @test Verify multiple init/deinit cycles work. */
TEST(Stm32Flash, MultipleInitDeinitCycles) {
  Stm32Flash flash;

  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(flash.init(), FlashStatus::OK);
    EXPECT_TRUE(flash.isInitialized());
    flash.deinit();
    EXPECT_FALSE(flash.isInitialized());
  }
}

/** @test Verify double init reinitializes cleanly (double-init guard). */
TEST(Stm32Flash, DoubleInitReinitializes) {
  Stm32Flash flash;

  EXPECT_EQ(flash.init(), FlashStatus::OK);
  EXPECT_TRUE(flash.isInitialized());

  // Init again without explicit deinit -- should succeed
  EXPECT_EQ(flash.init(), FlashStatus::OK);
  EXPECT_TRUE(flash.isInitialized());
}

/* ----------------------------- Read Tests ----------------------------- */

/** @test Verify read returns ERROR_NOT_INIT when not initialized. */
TEST(Stm32Flash, ReadNotInitialized) {
  Stm32Flash flash;
  uint8_t data[4] = {};

  const FlashStatus STATUS = flash.read(0x08000000, data, 4);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_NOT_INIT);
}

/** @test Verify read succeeds and returns erased data (0xFF). */
TEST(Stm32Flash, ReadSucceeds) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  uint8_t data[8] = {};
  const FlashStatus STATUS = flash.read(0x08000000, data, 8);

  EXPECT_EQ(STATUS, FlashStatus::OK);
  // Mock flash is initialized to 0xFF (erased state)
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(data[i], 0xFF);
  }
}

/** @test Verify read rejects null pointer. */
TEST(Stm32Flash, ReadNullPointer) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  const FlashStatus STATUS = flash.read(0x08000000, nullptr, 4);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify read rejects zero length. */
TEST(Stm32Flash, ReadZeroLength) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  uint8_t data[1] = {};
  const FlashStatus STATUS = flash.read(0x08000000, data, 0);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify read rejects out-of-range address. */
TEST(Stm32Flash, ReadOutOfRange) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  const auto GEO = flash.geometry();
  uint8_t data[4] = {};

  // Read past end of flash
  const FlashStatus STATUS = flash.read(GEO.baseAddress + GEO.totalSize, data, 4);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/* ----------------------------- Write Tests ----------------------------- */

/** @test Verify write returns ERROR_NOT_INIT when not initialized. */
TEST(Stm32Flash, WriteNotInitialized) {
  Stm32Flash flash;
  uint8_t data[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

  const FlashStatus STATUS = flash.write(0x08000000, data, 8);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_NOT_INIT);
}

/** @test Verify write succeeds in mock mode. */
TEST(Stm32Flash, WriteSucceeds) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  uint8_t data[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE};
  const FlashStatus STATUS = flash.write(0x08000000, data, 8);

  EXPECT_EQ(STATUS, FlashStatus::OK);
}

/** @test Verify write rejects null pointer. */
TEST(Stm32Flash, WriteNullPointer) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  const FlashStatus STATUS = flash.write(0x08000000, nullptr, 8);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify write rejects zero length. */
TEST(Stm32Flash, WriteZeroLength) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  uint8_t data[8] = {};
  const FlashStatus STATUS = flash.write(0x08000000, data, 0);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify write rejects unaligned address. */
TEST(Stm32Flash, WriteUnaligned) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  uint8_t data[8] = {};
  // Address 0x08000001 is not 8-byte aligned
  const FlashStatus STATUS = flash.write(0x08000001, data, 8);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_ALIGNMENT);
}

/** @test Verify write rejects out-of-range address. */
TEST(Stm32Flash, WriteOutOfRange) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  const auto GEO = flash.geometry();
  uint8_t data[8] = {};

  const FlashStatus STATUS = flash.write(GEO.baseAddress + GEO.totalSize, data, 8);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/* ----------------------------- Erase Tests ----------------------------- */

/** @test Verify erase returns ERROR_NOT_INIT when not initialized. */
TEST(Stm32Flash, EraseNotInitialized) {
  Stm32Flash flash;

  const FlashStatus STATUS = flash.erasePage(0);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_NOT_INIT);
}

/** @test Verify single page erase succeeds. */
TEST(Stm32Flash, ErasePageSucceeds) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  const FlashStatus STATUS = flash.erasePage(0);

  EXPECT_EQ(STATUS, FlashStatus::OK);
  EXPECT_EQ(flash.stats().pagesErased, 1U);
}

/** @test Verify multi-page erase succeeds. */
TEST(Stm32Flash, ErasePagesSucceeds) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  const FlashStatus STATUS = flash.erasePages(0, 4);

  EXPECT_EQ(STATUS, FlashStatus::OK);
  EXPECT_EQ(flash.stats().pagesErased, 4U);
}

/** @test Verify erase rejects invalid page index. */
TEST(Stm32Flash, EraseInvalidPage) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  const auto GEO = flash.geometry();
  const FlashStatus STATUS = flash.erasePage(GEO.pageCount);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify erase rejects range that exceeds page count. */
TEST(Stm32Flash, ErasePagesInvalidRange) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  const auto GEO = flash.geometry();
  // Start at last page, try to erase 2 pages
  const FlashStatus STATUS = flash.erasePages(GEO.pageCount - 1, 2);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify erase rejects zero count. */
TEST(Stm32Flash, ErasePagesZeroCount) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  const FlashStatus STATUS = flash.erasePages(0, 0);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/* ----------------------------- Read-After-Write Tests ----------------------------- */

/** @test Verify data can be written and read back. */
TEST(Stm32Flash, WriteAndReadBack) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  uint8_t tx[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
  uint8_t rx[16] = {};

  EXPECT_EQ(flash.write(0x08000000, tx, 16), FlashStatus::OK);
  EXPECT_EQ(flash.read(0x08000000, rx, 16), FlashStatus::OK);

  for (int i = 0; i < 16; ++i) {
    EXPECT_EQ(rx[i], tx[i]);
  }
}

/** @test Verify erase resets page data to 0xFF. */
TEST(Stm32Flash, EraseResetsToFF) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  // Write some data to page 0
  uint8_t tx[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};
  static_cast<void>(flash.write(0x08000000, tx, 8));

  // Erase page 0
  EXPECT_EQ(flash.erasePage(0), FlashStatus::OK);

  // Read back -- should be all 0xFF
  uint8_t rx[8] = {};
  static_cast<void>(flash.read(0x08000000, rx, 8));

  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(rx[i], 0xFF);
  }
}

/** @test Verify erase, write, read cycle works. */
TEST(Stm32Flash, EraseAndWriteAndRead) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  // Use page 5 (not page 0) to verify page addressing
  const uint32_t PAGE_ADDR = flash.addressForPage(5);

  // Erase the page
  EXPECT_EQ(flash.erasePage(5), FlashStatus::OK);

  // Write to start of page
  uint8_t tx[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE};
  EXPECT_EQ(flash.write(PAGE_ADDR, tx, 8), FlashStatus::OK);

  // Read back
  uint8_t rx[8] = {};
  EXPECT_EQ(flash.read(PAGE_ADDR, rx, 8), FlashStatus::OK);

  EXPECT_EQ(rx[0], 0xDE);
  EXPECT_EQ(rx[1], 0xAD);
  EXPECT_EQ(rx[2], 0xBE);
  EXPECT_EQ(rx[3], 0xEF);
  EXPECT_EQ(rx[4], 0xCA);
  EXPECT_EQ(rx[5], 0xFE);
  EXPECT_EQ(rx[6], 0xBA);
  EXPECT_EQ(rx[7], 0xBE);
}

/* ----------------------------- Stats Tests ----------------------------- */

/** @test Verify initial stats are zero. */
TEST(Stm32Flash, InitialStatsZero) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  const auto& STATS = flash.stats();

  EXPECT_EQ(STATS.bytesWritten, 0U);
  EXPECT_EQ(STATS.bytesRead, 0U);
  EXPECT_EQ(STATS.pagesErased, 0U);
  EXPECT_EQ(STATS.totalErrors(), 0U);
}

/** @test Verify write increments stats. */
TEST(Stm32Flash, WriteIncrementsStats) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  uint8_t data[16] = {};
  static_cast<void>(flash.write(0x08000000, data, 16));

  EXPECT_EQ(flash.stats().bytesWritten, 16U);
}

/** @test Verify read increments stats. */
TEST(Stm32Flash, ReadIncrementsStats) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  uint8_t data[32] = {};
  static_cast<void>(flash.read(0x08000000, data, 32));

  EXPECT_EQ(flash.stats().bytesRead, 32U);
}

/** @test Verify erase increments stats. */
TEST(Stm32Flash, EraseIncrementsStats) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  static_cast<void>(flash.erasePage(0));
  static_cast<void>(flash.erasePages(1, 3));

  EXPECT_EQ(flash.stats().pagesErased, 4U);
}

/** @test Verify failed operations do not increment stats. */
TEST(Stm32Flash, FailedOpsNoStats) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  // Null pointer -> ERROR_INVALID_ARG, no stats increment
  static_cast<void>(flash.write(0x08000000, nullptr, 8));
  static_cast<void>(flash.read(0x08000000, nullptr, 8));

  EXPECT_EQ(flash.stats().bytesWritten, 0U);
  EXPECT_EQ(flash.stats().bytesRead, 0U);
}

/** @test Verify resetStats clears counters. */
TEST(Stm32Flash, ResetStats) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  uint8_t data[8] = {};
  static_cast<void>(flash.write(0x08000000, data, 8));
  static_cast<void>(flash.read(0x08000000, data, 8));
  static_cast<void>(flash.erasePage(0));

  EXPECT_EQ(flash.stats().bytesWritten, 8U);

  flash.resetStats();

  EXPECT_EQ(flash.stats().bytesWritten, 0U);
  EXPECT_EQ(flash.stats().bytesRead, 0U);
  EXPECT_EQ(flash.stats().pagesErased, 0U);
}

/* ----------------------------- Geometry Tests ----------------------------- */

/** @test Verify geometry reports valid values. */
TEST(Stm32Flash, ReportsValidGeometry) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  const auto GEO = flash.geometry();

  EXPECT_EQ(GEO.baseAddress, 0x08000000U);
  EXPECT_GT(GEO.totalSize, 0U);
  EXPECT_GT(GEO.pageSize, 0U);
  EXPECT_GT(GEO.writeAlignment, 0U);
  EXPECT_GT(GEO.pageCount, 0U);
  EXPECT_GT(GEO.bankCount, 0U);
  // totalSize should equal pageSize * pageCount
  EXPECT_EQ(GEO.totalSize, GEO.pageSize * GEO.pageCount);
}

/** @test Verify pageForAddress returns correct page index. */
TEST(Stm32Flash, PageForAddress) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  const auto GEO = flash.geometry();

  // First byte of page 0
  EXPECT_EQ(flash.pageForAddress(GEO.baseAddress), 0U);

  // First byte of page 1
  EXPECT_EQ(flash.pageForAddress(GEO.baseAddress + GEO.pageSize), 1U);

  // Last byte of page 0
  EXPECT_EQ(flash.pageForAddress(GEO.baseAddress + GEO.pageSize - 1), 0U);

  // Middle of page 5
  EXPECT_EQ(flash.pageForAddress(GEO.baseAddress + 5 * GEO.pageSize + 100), 5U);
}

/** @test Verify addressForPage returns correct address. */
TEST(Stm32Flash, AddressForPage) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  const auto GEO = flash.geometry();

  EXPECT_EQ(flash.addressForPage(0), GEO.baseAddress);
  EXPECT_EQ(flash.addressForPage(1), GEO.baseAddress + GEO.pageSize);
  EXPECT_EQ(flash.addressForPage(5), GEO.baseAddress + 5 * GEO.pageSize);
}

/* ----------------------------- Stm32FlashOptions Tests ----------------------------- */

/** @test Verify Stm32FlashOptions default timeout. */
TEST(Stm32FlashOptions, DefaultValues) {
  Stm32FlashOptions opts;

  EXPECT_EQ(opts.timeoutMs, 5000U);
}

/** @test Verify Stm32FlashOptions can be aggregate-initialized. */
TEST(Stm32FlashOptions, AggregateInit) {
  Stm32FlashOptions opts = {10000};

  EXPECT_EQ(opts.timeoutMs, 10000U);
}

/** @test Verify init accepts Stm32FlashOptions in mock mode. */
TEST(Stm32Flash, InitWithOptions) {
  Stm32Flash flash;
  Stm32FlashOptions opts = {2000};

  const FlashStatus STATUS = flash.init(opts);

  EXPECT_EQ(STATUS, FlashStatus::OK);
  EXPECT_TRUE(flash.isInitialized());
}

/* ----------------------------- isBusy Tests ----------------------------- */

/** @test Verify isBusy returns false when not initialized. */
TEST(Stm32Flash, BusyNotInit) {
  Stm32Flash flash;
  EXPECT_FALSE(flash.isBusy());
}

/** @test Verify isBusy returns false in mock mode (no real peripheral). */
TEST(Stm32Flash, BusyAfterInit) {
  Stm32Flash flash;
  static_cast<void>(flash.init());

  EXPECT_FALSE(flash.isBusy());
}
