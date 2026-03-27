/**
 * @file CrcBitwise_uTest.cpp
 * @brief Unit tests for bitwise (table-less) CRC implementations.
 *
 * Tests all CRC variants (8/16/24/32/64-bit) against canonical check values
 * from the CRC catalog. Test vector is ASCII "123456789".
 */

#include "src/utilities/checksums/crc/inc/Crc.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using apex::checksums::crc::Crc16Ibm3740Bitwise;
using apex::checksums::crc::Crc16IbmSdlcBitwise;
using apex::checksums::crc::Crc16ModbusBitwise;
using apex::checksums::crc::Crc16UsbBitwise;
using apex::checksums::crc::Crc16XmodemBitwise;
using apex::checksums::crc::Crc24OpenpgpBitwise;
using apex::checksums::crc::Crc32IscsiBitwise;
using apex::checksums::crc::Crc32IsoHdlcBitwise;
using apex::checksums::crc::Crc32Mpeg2Bitwise;
using apex::checksums::crc::Crc64EcmaBitwise;
using apex::checksums::crc::Crc64XzBitwise;
using apex::checksums::crc::Crc8AutosarBitwise;
using apex::checksums::crc::Crc8MaximBitwise;
using apex::checksums::crc::Crc8SmbusBitwise;
using apex::checksums::crc::Status;

/// Canonical test vector: ASCII "123456789"
static const std::vector<uint8_t> CHECK_DATA = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

/* ----------------------------- CRC-8 Tests ----------------------------- */

/** @test CRC-8/SMBUS bitwise implementation against canonical check value. */
TEST(CrcBitwiseUnitTest, Crc8Smbus) {
  Crc8SmbusBitwise calc;
  uint8_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0xF4u);
}

/** @test CRC-8/MAXIM-DOW bitwise implementation against canonical check value. */
TEST(CrcBitwiseUnitTest, Crc8Maxim) {
  Crc8MaximBitwise calc;
  uint8_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0xA1u);
}

/** @test CRC-8/AUTOSAR bitwise implementation against canonical check value. */
TEST(CrcBitwiseUnitTest, Crc8Autosar) {
  Crc8AutosarBitwise calc;
  uint8_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0xDFu);
}

/* ----------------------------- CRC-16 Tests ----------------------------- */

/** @test CRC-16/XMODEM bitwise implementation against canonical check value. */
TEST(CrcBitwiseUnitTest, Crc16Xmodem) {
  Crc16XmodemBitwise calc;
  uint16_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x31C3u);
}

/** @test CRC-16/IBM-3740 bitwise implementation against canonical check value. */
TEST(CrcBitwiseUnitTest, Crc16Ibm3740) {
  Crc16Ibm3740Bitwise calc;
  uint16_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x29B1u);
}

/** @test CRC-16/IBM-SDLC bitwise implementation against canonical check value. */
TEST(CrcBitwiseUnitTest, Crc16IbmSdlc) {
  Crc16IbmSdlcBitwise calc;
  uint16_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x906Eu);
}

/** @test CRC-16/MODBUS bitwise implementation against canonical check value. */
TEST(CrcBitwiseUnitTest, Crc16Modbus) {
  Crc16ModbusBitwise calc;
  uint16_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x4B37u);
}

/** @test CRC-16/USB bitwise implementation against canonical check value. */
TEST(CrcBitwiseUnitTest, Crc16Usb) {
  Crc16UsbBitwise calc;
  uint16_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0xB4C8u);
}

/* ----------------------------- CRC-24 Tests ----------------------------- */

/** @test CRC-24/OPENPGP bitwise implementation against canonical check value. */
TEST(CrcBitwiseUnitTest, Crc24Openpgp) {
  Crc24OpenpgpBitwise calc;
  uint32_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x21CF02u);
}

/* ----------------------------- CRC-32 Tests ----------------------------- */

/** @test CRC-32/ISO-HDLC bitwise implementation against canonical check value. */
TEST(CrcBitwiseUnitTest, Crc32IsoHdlc) {
  Crc32IsoHdlcBitwise calc;
  uint32_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0xCBF43926u);
}

/** @test CRC-32/ISCSI bitwise implementation against canonical check value. */
TEST(CrcBitwiseUnitTest, Crc32Iscsi) {
  Crc32IscsiBitwise calc;
  uint32_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0xE3069283u);
}

/** @test CRC-32/MPEG-2 bitwise implementation against canonical check value. */
TEST(CrcBitwiseUnitTest, Crc32Mpeg2) {
  Crc32Mpeg2Bitwise calc;
  uint32_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x0376E6E7u);
}

/* ----------------------------- CRC-64 Tests ----------------------------- */

/** @test CRC-64/ECMA-182 bitwise implementation against canonical check value. */
TEST(CrcBitwiseUnitTest, Crc64Ecma) {
  Crc64EcmaBitwise calc;
  uint64_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x6C40DF5F0B497347ull);
}

/** @test CRC-64/XZ bitwise implementation against canonical check value. */
TEST(CrcBitwiseUnitTest, Crc64Xz) {
  Crc64XzBitwise calc;
  uint64_t out{};
  EXPECT_EQ(calc.calculate(CHECK_DATA, out), Status::SUCCESS);
  EXPECT_EQ(out, 0x995DC9BBDF1939FAull);
}
