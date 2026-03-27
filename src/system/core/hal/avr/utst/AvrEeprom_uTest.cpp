/**
 * @file AvrEeprom_uTest.cpp
 * @brief Unit tests for AvrEeprom implementation (using mock mode).
 *
 * These tests run on the host by defining APEX_HAL_AVR_MOCK,
 * which removes AVR hardware dependencies and provides a simulated
 * 1 KB EEPROM shadow buffer.
 */

#define APEX_HAL_AVR_MOCK 1

#include "src/system/core/hal/avr/inc/AvrEeprom.hpp"

#include <gtest/gtest.h>

using apex::hal::FlashGeometry;
using apex::hal::FlashStats;
using apex::hal::FlashStatus;
using apex::hal::avr::AvrEeprom;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verify AvrEeprom can be default constructed in mock mode. */
TEST(AvrEeprom, DefaultConstruction) {
  AvrEeprom eeprom;

  EXPECT_FALSE(eeprom.isInitialized());
  EXPECT_FALSE(eeprom.isBusy());
}

/* ----------------------------- Init/Deinit Tests ----------------------------- */

/** @test Verify init succeeds in mock mode. */
TEST(AvrEeprom, InitSucceeds) {
  AvrEeprom eeprom;

  const FlashStatus STATUS = eeprom.init();

  EXPECT_EQ(STATUS, FlashStatus::OK);
  EXPECT_TRUE(eeprom.isInitialized());
}

/** @test Verify deinit resets state. */
TEST(AvrEeprom, DeinitResetsState) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  eeprom.deinit();

  EXPECT_FALSE(eeprom.isInitialized());
}

/** @test Verify multiple init/deinit cycles work. */
TEST(AvrEeprom, MultipleInitDeinitCycles) {
  AvrEeprom eeprom;

  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(eeprom.init(), FlashStatus::OK);
    EXPECT_TRUE(eeprom.isInitialized());
    eeprom.deinit();
    EXPECT_FALSE(eeprom.isInitialized());
  }
}

/** @test Verify double init reinitializes cleanly (double-init guard). */
TEST(AvrEeprom, DoubleInitReinitializes) {
  AvrEeprom eeprom;

  EXPECT_EQ(eeprom.init(), FlashStatus::OK);
  EXPECT_TRUE(eeprom.isInitialized());

  // Init again without explicit deinit -- should succeed
  EXPECT_EQ(eeprom.init(), FlashStatus::OK);
  EXPECT_TRUE(eeprom.isInitialized());
}

/* ----------------------------- Read Tests ----------------------------- */

/** @test Verify read returns ERROR_NOT_INIT when not initialized. */
TEST(AvrEeprom, ReadNotInitialized) {
  AvrEeprom eeprom;
  uint8_t data[4] = {};

  const FlashStatus STATUS = eeprom.read(0, data, 4);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_NOT_INIT);
}

/** @test Verify read succeeds and returns erased data (0xFF). */
TEST(AvrEeprom, ReadSucceeds) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  uint8_t data[8] = {};
  const FlashStatus STATUS = eeprom.read(0, data, 8);

  EXPECT_EQ(STATUS, FlashStatus::OK);
  // Mock EEPROM is initialized to 0xFF (erased state)
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(data[i], 0xFF);
  }
}

/** @test Verify read rejects null pointer. */
TEST(AvrEeprom, ReadNullPointer) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  const FlashStatus STATUS = eeprom.read(0, nullptr, 4);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify read rejects zero length. */
TEST(AvrEeprom, ReadZeroLength) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  uint8_t data[1] = {};
  const FlashStatus STATUS = eeprom.read(0, data, 0);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify read rejects out-of-range address. */
TEST(AvrEeprom, ReadOutOfRange) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  const auto GEO = eeprom.geometry();
  uint8_t data[4] = {};

  // Read past end of EEPROM
  const FlashStatus STATUS = eeprom.read(GEO.totalSize, data, 4);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify read rejects length that extends past end. */
TEST(AvrEeprom, ReadExceedsEnd) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  const auto GEO = eeprom.geometry();
  uint8_t data[16] = {};

  // Start near end, length would overflow
  const FlashStatus STATUS = eeprom.read(GEO.totalSize - 4, data, 16);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/* ----------------------------- Write Tests ----------------------------- */

/** @test Verify write returns ERROR_NOT_INIT when not initialized. */
TEST(AvrEeprom, WriteNotInitialized) {
  AvrEeprom eeprom;
  uint8_t data[4] = {0x01, 0x02, 0x03, 0x04};

  const FlashStatus STATUS = eeprom.write(0, data, 4);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_NOT_INIT);
}

/** @test Verify write succeeds in mock mode. */
TEST(AvrEeprom, WriteSucceeds) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  uint8_t data[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE};
  const FlashStatus STATUS = eeprom.write(0, data, 8);

  EXPECT_EQ(STATUS, FlashStatus::OK);
}

/** @test Verify write rejects null pointer. */
TEST(AvrEeprom, WriteNullPointer) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  const FlashStatus STATUS = eeprom.write(0, nullptr, 8);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify write rejects zero length. */
TEST(AvrEeprom, WriteZeroLength) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  uint8_t data[8] = {};
  const FlashStatus STATUS = eeprom.write(0, data, 0);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify write rejects out-of-range address. */
TEST(AvrEeprom, WriteOutOfRange) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  const auto GEO = eeprom.geometry();
  uint8_t data[4] = {};

  const FlashStatus STATUS = eeprom.write(GEO.totalSize, data, 4);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/* ----------------------------- Erase Tests ----------------------------- */

/** @test Verify erase returns ERROR_NOT_INIT when not initialized. */
TEST(AvrEeprom, EraseNotInitialized) {
  AvrEeprom eeprom;

  const FlashStatus STATUS = eeprom.erasePage(0);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_NOT_INIT);
}

/** @test Verify single page erase succeeds. */
TEST(AvrEeprom, ErasePageSucceeds) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  const FlashStatus STATUS = eeprom.erasePage(0);

  EXPECT_EQ(STATUS, FlashStatus::OK);
  EXPECT_EQ(eeprom.stats().pagesErased, 1U);
}

/** @test Verify erase rejects invalid page index. */
TEST(AvrEeprom, EraseInvalidPage) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  const auto GEO = eeprom.geometry();
  const FlashStatus STATUS = eeprom.erasePage(GEO.pageCount);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/** @test Verify erase rejects zero count. */
TEST(AvrEeprom, ErasePagesZeroCount) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  const FlashStatus STATUS = eeprom.erasePages(0, 0);

  EXPECT_EQ(STATUS, FlashStatus::ERROR_INVALID_ARG);
}

/* ----------------------------- Read-After-Write Tests ----------------------------- */

/** @test Verify data can be written and read back. */
TEST(AvrEeprom, WriteAndReadBack) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  uint8_t tx[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
  uint8_t rx[16] = {};

  EXPECT_EQ(eeprom.write(0, tx, 16), FlashStatus::OK);
  EXPECT_EQ(eeprom.read(0, rx, 16), FlashStatus::OK);

  for (int i = 0; i < 16; ++i) {
    EXPECT_EQ(rx[i], tx[i]);
  }
}

/** @test Verify write at non-zero offset reads back correctly. */
TEST(AvrEeprom, WriteAtOffset) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  uint8_t tx[4] = {0xAA, 0xBB, 0xCC, 0xDD};
  uint8_t rx[4] = {};
  const uint32_t OFFSET = 512;

  EXPECT_EQ(eeprom.write(OFFSET, tx, 4), FlashStatus::OK);
  EXPECT_EQ(eeprom.read(OFFSET, rx, 4), FlashStatus::OK);

  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(rx[i], tx[i]);
  }
}

/** @test Verify erase resets data to 0xFF. */
TEST(AvrEeprom, EraseResetsToFF) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  // Write some data
  uint8_t tx[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};
  static_cast<void>(eeprom.write(0, tx, 8));

  // Erase the page (single page covers entire EEPROM)
  EXPECT_EQ(eeprom.erasePage(0), FlashStatus::OK);

  // Read back -- should be all 0xFF
  uint8_t rx[8] = {};
  static_cast<void>(eeprom.read(0, rx, 8));

  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(rx[i], 0xFF);
  }
}

/** @test Verify write at last valid byte succeeds. */
TEST(AvrEeprom, WriteBoundaryLastByte) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  const auto GEO = eeprom.geometry();
  uint8_t tx = 0x42;
  uint8_t rx = 0;

  EXPECT_EQ(eeprom.write(GEO.totalSize - 1, &tx, 1), FlashStatus::OK);
  EXPECT_EQ(eeprom.read(GEO.totalSize - 1, &rx, 1), FlashStatus::OK);
  EXPECT_EQ(rx, 0x42);
}

/* ----------------------------- Stats Tests ----------------------------- */

/** @test Verify initial stats are zero. */
TEST(AvrEeprom, InitialStatsZero) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  const auto& STATS = eeprom.stats();

  EXPECT_EQ(STATS.bytesWritten, 0U);
  EXPECT_EQ(STATS.bytesRead, 0U);
  EXPECT_EQ(STATS.pagesErased, 0U);
  EXPECT_EQ(STATS.totalErrors(), 0U);
}

/** @test Verify write increments stats. */
TEST(AvrEeprom, WriteIncrementsStats) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  uint8_t data[16] = {};
  static_cast<void>(eeprom.write(0, data, 16));

  EXPECT_EQ(eeprom.stats().bytesWritten, 16U);
}

/** @test Verify read increments stats. */
TEST(AvrEeprom, ReadIncrementsStats) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  uint8_t data[32] = {};
  static_cast<void>(eeprom.read(0, data, 32));

  EXPECT_EQ(eeprom.stats().bytesRead, 32U);
}

/** @test Verify erase increments stats. */
TEST(AvrEeprom, EraseIncrementsStats) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  static_cast<void>(eeprom.erasePage(0));

  EXPECT_EQ(eeprom.stats().pagesErased, 1U);
}

/** @test Verify failed operations do not increment stats. */
TEST(AvrEeprom, FailedOpsNoStats) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  // Null pointer -> ERROR_INVALID_ARG, no stats increment
  static_cast<void>(eeprom.write(0, nullptr, 8));
  static_cast<void>(eeprom.read(0, nullptr, 8));

  EXPECT_EQ(eeprom.stats().bytesWritten, 0U);
  EXPECT_EQ(eeprom.stats().bytesRead, 0U);
}

/** @test Verify resetStats clears counters. */
TEST(AvrEeprom, ResetStats) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  uint8_t data[8] = {};
  static_cast<void>(eeprom.write(0, data, 8));
  static_cast<void>(eeprom.read(0, data, 8));
  static_cast<void>(eeprom.erasePage(0));

  EXPECT_EQ(eeprom.stats().bytesWritten, 8U);

  eeprom.resetStats();

  EXPECT_EQ(eeprom.stats().bytesWritten, 0U);
  EXPECT_EQ(eeprom.stats().bytesRead, 0U);
  EXPECT_EQ(eeprom.stats().pagesErased, 0U);
}

/* ----------------------------- Geometry Tests ----------------------------- */

/** @test Verify geometry reports valid values for ATmega328P EEPROM. */
TEST(AvrEeprom, ReportsValidGeometry) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  const auto GEO = eeprom.geometry();

  EXPECT_EQ(GEO.baseAddress, 0U);
  EXPECT_EQ(GEO.totalSize, 1024U);
  EXPECT_EQ(GEO.pageSize, 1024U);    // Entire EEPROM as one page
  EXPECT_EQ(GEO.writeAlignment, 1U); // Byte-granular
  EXPECT_EQ(GEO.pageCount, 1U);
  EXPECT_EQ(GEO.bankCount, 1U);
}

/** @test Verify pageForAddress always returns 0 (single page). */
TEST(AvrEeprom, PageForAddress) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  EXPECT_EQ(eeprom.pageForAddress(0), 0U);
  EXPECT_EQ(eeprom.pageForAddress(512), 0U);
  EXPECT_EQ(eeprom.pageForAddress(1023), 0U);
}

/** @test Verify addressForPage returns 0 (single page starts at 0). */
TEST(AvrEeprom, AddressForPage) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  EXPECT_EQ(eeprom.addressForPage(0), 0U);
}

/* ----------------------------- isBusy Tests ----------------------------- */

/** @test Verify isBusy returns false in mock mode. */
TEST(AvrEeprom, BusyReturnsFalseInMock) {
  AvrEeprom eeprom;
  static_cast<void>(eeprom.init());

  EXPECT_FALSE(eeprom.isBusy());
}
