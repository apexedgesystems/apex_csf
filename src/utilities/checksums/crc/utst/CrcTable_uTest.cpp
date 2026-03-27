/**
 * @file CrcTable_uTest.cpp
 * @brief Unit tests for table-driven CRC implementations.
 *
 * Tests all CRC variants (8/16/24/32/64-bit) against canonical check values
 * from the CRC catalog. Test vector is ASCII "123456789".
 */

#include "src/utilities/checksums/crc/inc/Crc.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using apex::checksums::crc::Crc16Ibm3740Table;
using apex::checksums::crc::Crc16IbmSdlcTable;
using apex::checksums::crc::Crc16ModbusTable;
using apex::checksums::crc::Crc16UsbTable;
using apex::checksums::crc::Crc16XmodemTable;
using apex::checksums::crc::Crc24OpenpgpTable;
using apex::checksums::crc::Crc32IscsiHardware;
using apex::checksums::crc::Crc32IscsiTable;
using apex::checksums::crc::Crc32IsoHdlcTable;
using apex::checksums::crc::Crc32Mpeg2Table;
using apex::checksums::crc::Crc64EcmaTable;
using apex::checksums::crc::Crc64XzTable;
using apex::checksums::crc::Crc8AutosarTable;
using apex::checksums::crc::Crc8MaximTable;
using apex::checksums::crc::Crc8SmbusTable;
using apex::checksums::crc::Status;

/// Canonical test vector: ASCII "123456789"
static const std::vector<uint8_t> CHECK_DATA = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

/* ----------------------------- CRC-8 Tests ----------------------------- */

/** @test CRC-8/SMBUS table implementation against canonical check value. */
TEST(CrcTableUnitTest, Crc8Smbus) {
  Crc8SmbusTable calc;
  uint8_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0xF4u);
}

/** @test CRC-8/MAXIM-DOW table implementation against canonical check value. */
TEST(CrcTableUnitTest, Crc8Maxim) {
  Crc8MaximTable calc;
  uint8_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0xA1u);
}

/** @test CRC-8/AUTOSAR table implementation against canonical check value. */
TEST(CrcTableUnitTest, Crc8Autosar) {
  Crc8AutosarTable calc;
  uint8_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0xDFu);
}

/* ----------------------------- CRC-16 Tests ----------------------------- */

/** @test CRC-16/XMODEM table implementation against canonical check value. */
TEST(CrcTableUnitTest, Crc16Xmodem) {
  Crc16XmodemTable calc;
  uint16_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x31C3u);
}

/** @test CRC-16/IBM-3740 table implementation against canonical check value. */
TEST(CrcTableUnitTest, Crc16Ibm3740) {
  Crc16Ibm3740Table calc;
  uint16_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x29B1u);
}

/** @test CRC-16/IBM-SDLC table implementation against canonical check value. */
TEST(CrcTableUnitTest, Crc16IbmSdlc) {
  Crc16IbmSdlcTable calc;
  uint16_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x906Eu);
}

/** @test CRC-16/MODBUS table implementation against canonical check value. */
TEST(CrcTableUnitTest, Crc16Modbus) {
  Crc16ModbusTable calc;
  uint16_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x4B37u);
}

/** @test CRC-16/USB table implementation against canonical check value. */
TEST(CrcTableUnitTest, Crc16Usb) {
  Crc16UsbTable calc;
  uint16_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0xB4C8u);
}

/* ----------------------------- CRC-24 Tests ----------------------------- */

/** @test CRC-24/OPENPGP table implementation against canonical check value. */
TEST(CrcTableUnitTest, Crc24Openpgp) {
  Crc24OpenpgpTable calc;
  uint32_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x21CF02u);
}

/* ----------------------------- CRC-32 Tests ----------------------------- */

/** @test CRC-32/ISO-HDLC table implementation against canonical check value. */
TEST(CrcTableUnitTest, Crc32IsoHdlc) {
  Crc32IsoHdlcTable calc;
  uint32_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0xCBF43926u);
}

/** @test CRC-32/ISCSI table implementation against canonical check value. */
TEST(CrcTableUnitTest, Crc32Iscsi) {
  Crc32IscsiTable calc;
  uint32_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0xE3069283u);
}

/** @test CRC-32/ISCSI hardware implementation against canonical check value. */
TEST(CrcTableUnitTest, Crc32IscsiHardware) {
  Crc32IscsiHardware calc;
  uint32_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0xE3069283u);
}

/** @test CRC-32/MPEG-2 table implementation against canonical check value. */
TEST(CrcTableUnitTest, Crc32Mpeg2) {
  Crc32Mpeg2Table calc;
  uint32_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x0376E6E7u);
}

/* ----------------------------- CRC-64 Tests ----------------------------- */

/** @test CRC-64/ECMA-182 table implementation against canonical check value. */
TEST(CrcTableUnitTest, Crc64Ecma) {
  Crc64EcmaTable calc;
  uint64_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x6C40DF5F0B497347ull);
}

/** @test CRC-64/XZ table implementation against canonical check value. */
TEST(CrcTableUnitTest, Crc64Xz) {
  Crc64XzTable calc;
  uint64_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x995DC9BBDF1939FAull);
}
