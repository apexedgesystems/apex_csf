/**
 * @file CrcNibble_uTest.cpp
 * @brief Unit tests for nibble-table (16-entry) CRC implementations.
 *
 * Tests all CRC variants (8/16/24/32/64-bit) against canonical check values
 * from the CRC catalog. Test vector is ASCII "123456789".
 */

#include "src/utilities/checksums/crc/inc/Crc.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using apex::checksums::crc::Crc16Ibm3740Nibble;
using apex::checksums::crc::Crc16IbmSdlcNibble;
using apex::checksums::crc::Crc16ModbusNibble;
using apex::checksums::crc::Crc16UsbNibble;
using apex::checksums::crc::Crc16XmodemNibble;
using apex::checksums::crc::Crc24OpenpgpNibble;
using apex::checksums::crc::Crc32IscsiNibble;
using apex::checksums::crc::Crc32IsoHdlcNibble;
using apex::checksums::crc::Crc32Mpeg2Nibble;
using apex::checksums::crc::Crc64EcmaNibble;
using apex::checksums::crc::Crc64XzNibble;
using apex::checksums::crc::Crc8AutosarNibble;
using apex::checksums::crc::Crc8MaximNibble;
using apex::checksums::crc::Crc8SmbusNibble;
using apex::checksums::crc::Status;

/// Canonical test vector: ASCII "123456789"
static const std::vector<uint8_t> CHECK_DATA = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

/* ----------------------------- CRC-8 Tests ----------------------------- */

/** @test CRC-8/SMBUS nibble implementation against canonical check value. */
TEST(CrcNibbleUnitTest, Crc8Smbus) {
  Crc8SmbusNibble calc;
  uint8_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0xF4u);
}

/** @test CRC-8/MAXIM-DOW nibble implementation against canonical check value. */
TEST(CrcNibbleUnitTest, Crc8Maxim) {
  Crc8MaximNibble calc;
  uint8_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0xA1u);
}

/** @test CRC-8/AUTOSAR nibble implementation against canonical check value. */
TEST(CrcNibbleUnitTest, Crc8Autosar) {
  Crc8AutosarNibble calc;
  uint8_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0xDFu);
}

/* ----------------------------- CRC-16 Tests ----------------------------- */

/** @test CRC-16/XMODEM nibble implementation against canonical check value. */
TEST(CrcNibbleUnitTest, Crc16Xmodem) {
  Crc16XmodemNibble calc;
  uint16_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x31C3u);
}

/** @test CRC-16/IBM-3740 nibble implementation against canonical check value. */
TEST(CrcNibbleUnitTest, Crc16Ibm3740) {
  Crc16Ibm3740Nibble calc;
  uint16_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x29B1u);
}

/** @test CRC-16/IBM-SDLC nibble implementation against canonical check value. */
TEST(CrcNibbleUnitTest, Crc16IbmSdlc) {
  Crc16IbmSdlcNibble calc;
  uint16_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x906Eu);
}

/** @test CRC-16/MODBUS nibble implementation against canonical check value. */
TEST(CrcNibbleUnitTest, Crc16Modbus) {
  Crc16ModbusNibble calc;
  uint16_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x4B37u);
}

/** @test CRC-16/USB nibble implementation against canonical check value. */
TEST(CrcNibbleUnitTest, Crc16Usb) {
  Crc16UsbNibble calc;
  uint16_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0xB4C8u);
}

/* ----------------------------- CRC-24 Tests ----------------------------- */

/** @test CRC-24/OPENPGP nibble implementation against canonical check value. */
TEST(CrcNibbleUnitTest, Crc24Openpgp) {
  Crc24OpenpgpNibble calc;
  uint32_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x21CF02u);
}

/* ----------------------------- CRC-32 Tests ----------------------------- */

/** @test CRC-32/ISO-HDLC nibble implementation against canonical check value. */
TEST(CrcNibbleUnitTest, Crc32IsoHdlc) {
  Crc32IsoHdlcNibble calc;
  uint32_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0xCBF43926u);
}

/** @test CRC-32/ISCSI nibble implementation against canonical check value. */
TEST(CrcNibbleUnitTest, Crc32Iscsi) {
  Crc32IscsiNibble calc;
  uint32_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0xE3069283u);
}

/** @test CRC-32/MPEG-2 nibble implementation against canonical check value. */
TEST(CrcNibbleUnitTest, Crc32Mpeg2) {
  Crc32Mpeg2Nibble calc;
  uint32_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x0376E6E7u);
}

/* ----------------------------- CRC-64 Tests ----------------------------- */

/** @test CRC-64/ECMA-182 nibble implementation against canonical check value. */
TEST(CrcNibbleUnitTest, Crc64Ecma) {
  Crc64EcmaNibble calc;
  uint64_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x6C40DF5F0B497347ull);
}

/** @test CRC-64/XZ nibble implementation against canonical check value. */
TEST(CrcNibbleUnitTest, Crc64Xz) {
  Crc64XzNibble calc;
  uint64_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x995DC9BBDF1939FAull);
}
