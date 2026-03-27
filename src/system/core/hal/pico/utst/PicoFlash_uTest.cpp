/**
 * @file PicoFlash_uTest.cpp
 * @brief Unit tests for PicoFlash implementation (using mock mode).
 *
 * These tests run on the host by defining APEX_HAL_PICO_MOCK,
 * which removes Pico SDK dependencies and provides a simulated
 * 2 MB flash region (512 sectors of 4 KB).
 */

#define APEX_HAL_PICO_MOCK 1

#include "src/system/core/hal/pico/inc/PicoFlash.hpp"

#include <gtest/gtest.h>

using apex::hal::FlashGeometry;
using apex::hal::FlashStats;
using apex::hal::FlashStatus;
using apex::hal::pico::PicoFlash;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verify PicoFlash can be default constructed in mock mode. */
TEST(PicoFlash, DefaultConstruction) {
  PicoFlash flash;

  EXPECT_FALSE(flash.isInitialized());
  EXPECT_FALSE(flash.isBusy());
}

/* ----------------------------- Init/Deinit Tests ----------------------------- */

/** @test Verify init succeeds in mock mode. */
TEST(PicoFlash, InitSucceeds) {
  PicoFlash flash;

  const FlashStatus STATUS = flash.init();

  EXPECT_EQ(STATUS, FlashStatus::OK);
  EXPECT_TRUE(flash.isInitialized());
}

/** @test Verify deinit resets state. */
TEST(PicoFlash, DeinitResetsState) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  flash.deinit();

  EXPECT_FALSE(flash.isInitialized());
}

/** @test Verify multiple init/deinit cycles work. */
TEST(PicoFlash, MultipleInitDeinitCycles) {
  PicoFlash flash;

  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(flash.init(), FlashStatus::OK);
    EXPECT_TRUE(flash.isInitialized());
    flash.deinit();
    EXPECT_FALSE(flash.isInitialized());
  }
}

/** @test Verify double init reinitializes cleanly. */
TEST(PicoFlash, DoubleInitReinitializes) {
  PicoFlash flash;

  EXPECT_EQ(flash.init(), FlashStatus::OK);
  EXPECT_TRUE(flash.isInitialized());

  // Init again without explicit deinit -- should succeed
  EXPECT_EQ(flash.init(), FlashStatus::OK);
  EXPECT_TRUE(flash.isInitialized());
}

/* ----------------------------- Read Tests ----------------------------- */

/** @test Verify read returns ERROR_NOT_INIT when not initialized. */
TEST(PicoFlash, ReadNotInitialized) {
  PicoFlash flash;
  uint8_t data[4] = {};

  const FlashStatus STATUS = flash.read(0x10000000, data, 4);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_NOT_INIT);
}

/** @test Verify read succeeds and returns erased data (0xFF). */
TEST(PicoFlash, ReadSucceeds) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  uint8_t data[8] = {};
  const FlashStatus STATUS = flash.read(0x10000000, data, 8);

  EXPECT_EQ(STATUS, FlashStatus::OK);
  // Mock flash is initialized to 0xFF (erased state)
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(data[i], 0xFF);
  }
}

/** @test Verify read rejects null pointer. */
TEST(PicoFlash, ReadNullPointer) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  const FlashStatus STATUS = flash.read(0x10000000, nullptr, 4);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify read rejects zero length. */
TEST(PicoFlash, ReadZeroLength) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  uint8_t data[1] = {};
  const FlashStatus STATUS = flash.read(0x10000000, data, 0);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify read rejects out-of-range address. */
TEST(PicoFlash, ReadOutOfRange) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  const auto GEO = flash.geometry();
  uint8_t data[4] = {};

  // Read past end of flash
  const FlashStatus STATUS = flash.read(GEO.baseAddress + GEO.totalSize, data, 4);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/* ----------------------------- Write Tests ----------------------------- */

/** @test Verify write returns ERROR_NOT_INIT when not initialized. */
TEST(PicoFlash, WriteNotInitialized) {
  PicoFlash flash;
  uint8_t data[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

  const FlashStatus STATUS = flash.write(0x10000000, data, 8);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_NOT_INIT);
}

/** @test Verify write succeeds in mock mode. */
TEST(PicoFlash, WriteSucceeds) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  uint8_t data[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE};
  const FlashStatus STATUS = flash.write(0x10000000, data, 8);

  EXPECT_EQ(STATUS, FlashStatus::OK);
}

/** @test Verify write rejects null pointer. */
TEST(PicoFlash, WriteNullPointer) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  const FlashStatus STATUS = flash.write(0x10000000, nullptr, 8);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify write rejects zero length. */
TEST(PicoFlash, WriteZeroLength) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  uint8_t data[8] = {};
  const FlashStatus STATUS = flash.write(0x10000000, data, 0);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify write rejects out-of-range address. */
TEST(PicoFlash, WriteOutOfRange) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  const auto GEO = flash.geometry();
  uint8_t data[8] = {};

  const FlashStatus STATUS = flash.write(GEO.baseAddress + GEO.totalSize, data, 8);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/* ----------------------------- Erase Tests ----------------------------- */

/** @test Verify erase returns ERROR_NOT_INIT when not initialized. */
TEST(PicoFlash, EraseNotInitialized) {
  PicoFlash flash;

  const FlashStatus STATUS = flash.erasePage(0);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_NOT_INIT);
}

/** @test Verify single page erase succeeds. */
TEST(PicoFlash, ErasePageSucceeds) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  const FlashStatus STATUS = flash.erasePage(0);

  EXPECT_EQ(STATUS, FlashStatus::OK);
  EXPECT_EQ(flash.stats().pagesErased, 1U);
}

/** @test Verify multi-page erase succeeds. */
TEST(PicoFlash, ErasePagesSucceeds) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  const FlashStatus STATUS = flash.erasePages(0, 4);

  EXPECT_EQ(STATUS, FlashStatus::OK);
  EXPECT_EQ(flash.stats().pagesErased, 4U);
}

/** @test Verify erase rejects invalid page index. */
TEST(PicoFlash, EraseInvalidPage) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  const auto GEO = flash.geometry();
  const FlashStatus STATUS = flash.erasePage(GEO.pageCount);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify erase rejects range that exceeds page count. */
TEST(PicoFlash, ErasePagesInvalidRange) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  const auto GEO = flash.geometry();
  // Start at last page, try to erase 2 pages
  const FlashStatus STATUS = flash.erasePages(GEO.pageCount - 1, 2);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify erase rejects zero count. */
TEST(PicoFlash, ErasePagesZeroCount) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  const FlashStatus STATUS = flash.erasePages(0, 0);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify erase resets page data to 0xFF. */
TEST(PicoFlash, EraseResetsToFF) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  // Write some data to page 0
  uint8_t tx[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};
  static_cast<void>(flash.write(0x10000000, tx, 8));

  // Erase page 0
  EXPECT_EQ(flash.erasePage(0), FlashStatus::OK);

  // Read back -- should be all 0xFF
  uint8_t rx[8] = {};
  static_cast<void>(flash.read(0x10000000, rx, 8));

  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(rx[i], 0xFF);
  }
}

/* ----------------------------- Read-After-Write Tests ----------------------------- */

/** @test Verify data can be written and read back. */
TEST(PicoFlash, WriteAndReadBack) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  uint8_t tx[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
  uint8_t rx[16] = {};

  EXPECT_EQ(flash.write(0x10000000, tx, 16), FlashStatus::OK);
  EXPECT_EQ(flash.read(0x10000000, rx, 16), FlashStatus::OK);

  for (int i = 0; i < 16; ++i) {
    EXPECT_EQ(rx[i], tx[i]);
  }
}

/** @test Verify erase, write, read cycle works on non-zero page. */
TEST(PicoFlash, EraseAndWriteAndRead) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  // Use page 5 to verify page addressing
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

/* ----------------------------- Geometry Tests ----------------------------- */

/** @test Verify geometry reports 2 MB flash with 4 KB sectors. */
TEST(PicoFlash, ReportsValidGeometry) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  const auto GEO = flash.geometry();

  EXPECT_EQ(GEO.baseAddress, 0x10000000U);
  EXPECT_EQ(GEO.totalSize, 2U * 1024U * 1024U);
  EXPECT_EQ(GEO.pageSize, 4096U);
  EXPECT_EQ(GEO.writeAlignment, 256U);
  EXPECT_EQ(GEO.pageCount, 512U);
  EXPECT_EQ(GEO.bankCount, 1U);
  EXPECT_EQ(GEO.totalSize, GEO.pageSize * GEO.pageCount);
}

/** @test Verify pageForAddress returns correct page index. */
TEST(PicoFlash, PageForAddress) {
  PicoFlash flash;
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
TEST(PicoFlash, AddressForPage) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  const auto GEO = flash.geometry();

  EXPECT_EQ(flash.addressForPage(0), GEO.baseAddress);
  EXPECT_EQ(flash.addressForPage(1), GEO.baseAddress + GEO.pageSize);
  EXPECT_EQ(flash.addressForPage(5), GEO.baseAddress + 5 * GEO.pageSize);
}

/* ----------------------------- Stats Tests ----------------------------- */

/** @test Verify initial stats are zero. */
TEST(PicoFlash, InitialStatsZero) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  const auto& STATS = flash.stats();

  EXPECT_EQ(STATS.bytesWritten, 0U);
  EXPECT_EQ(STATS.bytesRead, 0U);
  EXPECT_EQ(STATS.pagesErased, 0U);
  EXPECT_EQ(STATS.totalErrors(), 0U);
}

/** @test Verify write increments stats. */
TEST(PicoFlash, WriteIncrementsStats) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  uint8_t data[16] = {};
  static_cast<void>(flash.write(0x10000000, data, 16));

  EXPECT_EQ(flash.stats().bytesWritten, 16U);
}

/** @test Verify read increments stats. */
TEST(PicoFlash, ReadIncrementsStats) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  uint8_t data[32] = {};
  static_cast<void>(flash.read(0x10000000, data, 32));

  EXPECT_EQ(flash.stats().bytesRead, 32U);
}

/** @test Verify failed operations do not increment stats. */
TEST(PicoFlash, FailedOpsNoStats) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  // Null pointer -> ERROR_INVALID_ARG, no stats increment
  static_cast<void>(flash.write(0x10000000, nullptr, 8));
  static_cast<void>(flash.read(0x10000000, nullptr, 8));

  EXPECT_EQ(flash.stats().bytesWritten, 0U);
  EXPECT_EQ(flash.stats().bytesRead, 0U);
}

/** @test Verify resetStats clears counters. */
TEST(PicoFlash, ResetStats) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  uint8_t data[8] = {};
  static_cast<void>(flash.write(0x10000000, data, 8));
  static_cast<void>(flash.read(0x10000000, data, 8));
  static_cast<void>(flash.erasePage(0));

  EXPECT_EQ(flash.stats().bytesWritten, 8U);

  flash.resetStats();

  EXPECT_EQ(flash.stats().bytesWritten, 0U);
  EXPECT_EQ(flash.stats().bytesRead, 0U);
  EXPECT_EQ(flash.stats().pagesErased, 0U);
}

/* ----------------------------- isBusy Tests ----------------------------- */

/** @test Verify isBusy returns false when not initialized. */
TEST(PicoFlash, BusyNotInit) {
  PicoFlash flash;
  EXPECT_FALSE(flash.isBusy());
}

/** @test Verify isBusy returns false in mock mode (no real peripheral). */
TEST(PicoFlash, BusyAfterInit) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  EXPECT_FALSE(flash.isBusy());
}

/* ----------------------------- Boundary Tests ----------------------------- */

/** @test Verify write at last valid address succeeds. */
TEST(PicoFlash, WriteBoundaryLastByte) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  const auto GEO = flash.geometry();
  const uint32_t LAST_ADDR = GEO.baseAddress + GEO.totalSize - 1;
  uint8_t data = 0x42;

  const FlashStatus STATUS = flash.write(LAST_ADDR, &data, 1);

  EXPECT_EQ(STATUS, FlashStatus::OK);
}

/** @test Verify read below base address is rejected. */
TEST(PicoFlash, ReadBelowBaseAddress) {
  PicoFlash flash;
  static_cast<void>(flash.init());

  uint8_t data[4] = {};
  const FlashStatus STATUS = flash.read(0x0FFFFFFF, data, 4);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}
