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

/* ----------------------------- Sector Layout ----------------------------- */

/** @test Verify the single-bank 2 MB layout: 4 x 32K, 128K, 7 x 256K. */
TEST(Stm32Flash, SectorLayoutSingleBank2MB) {
  uint32_t sizes[Stm32Flash::MAX_SECTORS] = {};
  const uint32_t COUNT =
      Stm32Flash::sectorLayout(2U * 1024U * 1024U, false, sizes, Stm32Flash::MAX_SECTORS);
  ASSERT_EQ(COUNT, 12U);
  uint32_t total = 0;
  for (uint32_t i = 0; i < COUNT; ++i) {
    const uint32_t EXPECTED = (i < 4U) ? 32U * 1024U : (i == 4U) ? 128U * 1024U : 256U * 1024U;
    EXPECT_EQ(sizes[i], EXPECTED) << "sector " << i;
    total += sizes[i];
  }
  EXPECT_EQ(total, 2U * 1024U * 1024U);
}

/** @test Verify dual-bank halves every sector and numbers the second bank after the first. */
TEST(Stm32Flash, SectorLayoutDualBank2MB) {
  uint32_t sizes[Stm32Flash::MAX_SECTORS] = {};
  const uint32_t COUNT =
      Stm32Flash::sectorLayout(2U * 1024U * 1024U, true, sizes, Stm32Flash::MAX_SECTORS);
  ASSERT_EQ(COUNT, 24U);
  for (uint32_t bank = 0; bank < 2U; ++bank) {
    uint32_t bankTotal = 0;
    for (uint32_t i = 0; i < 12U; ++i) {
      const uint32_t EXPECTED = (i < 4U) ? 16U * 1024U : (i == 4U) ? 64U * 1024U : 128U * 1024U;
      EXPECT_EQ(sizes[bank * 12U + i], EXPECTED) << "bank " << bank << " sector " << i;
      bankTotal += sizes[bank * 12U + i];
    }
    EXPECT_EQ(bankTotal, 1024U * 1024U);
  }
}

/** @test Verify a 1 MB single-bank part stops after three large sectors. */
TEST(Stm32Flash, SectorLayoutSingleBank1MB) {
  uint32_t sizes[Stm32Flash::MAX_SECTORS] = {};
  const uint32_t COUNT =
      Stm32Flash::sectorLayout(1024U * 1024U, false, sizes, Stm32Flash::MAX_SECTORS);
  ASSERT_EQ(COUNT, 8U);
  EXPECT_EQ(sizes[7], 256U * 1024U);
}

/** @test Verify the layout refuses a null table, a zero size, and an undersized table. */
TEST(Stm32Flash, SectorLayoutRejectsBadArguments) {
  uint32_t sizes[Stm32Flash::MAX_SECTORS] = {};
  EXPECT_EQ(Stm32Flash::sectorLayout(2U * 1024U * 1024U, false, nullptr, 4), 0U);
  EXPECT_EQ(Stm32Flash::sectorLayout(0, false, sizes, Stm32Flash::MAX_SECTORS), 0U);
  EXPECT_EQ(Stm32Flash::sectorLayout(2U * 1024U * 1024U, false, sizes, 4), 0U);
}

/* ----------------------------- Page Size Accessor ----------------------------- */

/** @test Verify pageSizeAt agrees with the uniform geometry and addresses chain by size. */
TEST(Stm32Flash, PageSizeAtMatchesGeometry) {
  Stm32Flash flash;
  ASSERT_EQ(flash.init(), FlashStatus::OK);
  const FlashGeometry GEO = flash.geometry();
  uint32_t expectedAddr = GEO.baseAddress;
  for (uint32_t i = 0; i < GEO.pageCount; ++i) {
    EXPECT_EQ(flash.pageSizeAt(i), GEO.pageSize) << "page " << i;
    EXPECT_EQ(flash.addressForPage(i), expectedAddr) << "page " << i;
    EXPECT_EQ(flash.pageForAddress(expectedAddr), i) << "page " << i;
    expectedAddr += flash.pageSizeAt(i);
  }
  EXPECT_EQ(flash.pageSizeAt(GEO.pageCount), 0U);
}
