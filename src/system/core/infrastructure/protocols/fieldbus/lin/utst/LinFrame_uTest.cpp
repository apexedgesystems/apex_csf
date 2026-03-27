/**
 * @file LinFrame_uTest.cpp
 * @brief Unit tests for LIN frame building and parsing.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/lin/inc/LinFrame.hpp"

#include <gtest/gtest.h>

#include <cstring>

namespace lin = apex::protocols::fieldbus::lin;

/* ----------------------------- Constants ----------------------------- */

/** @test Sync byte is 0x55. */
TEST(LinFrameTest, SyncByteValue) { EXPECT_EQ(lin::Constants::SYNC_BYTE, 0x55); }

/** @test Maximum data length is 8. */
TEST(LinFrameTest, MaxDataLength) { EXPECT_EQ(lin::Constants::MAX_DATA_LENGTH, 8); }

/** @test ID mask is 0x3F (6 bits). */
TEST(LinFrameTest, IdMask) { EXPECT_EQ(lin::Constants::ID_MASK, 0x3F); }

/** @test Master request ID is 0x3C. */
TEST(LinFrameTest, MasterRequestId) { EXPECT_EQ(lin::Constants::MASTER_REQUEST_ID, 0x3C); }

/** @test Slave response ID is 0x3D. */
TEST(LinFrameTest, SlaveResponseId) { EXPECT_EQ(lin::Constants::SLAVE_RESPONSE_ID, 0x3D); }

/* ----------------------------- Frame ID Validation ----------------------------- */

/** @test Valid frame IDs (0-63). */
TEST(LinFrameTest, ValidFrameIds) {
  EXPECT_TRUE(lin::isValidFrameId(0));
  EXPECT_TRUE(lin::isValidFrameId(32));
  EXPECT_TRUE(lin::isValidFrameId(63));
}

/** @test Invalid frame IDs (> 63). */
TEST(LinFrameTest, InvalidFrameIds) {
  EXPECT_FALSE(lin::isValidFrameId(64));
  EXPECT_FALSE(lin::isValidFrameId(128));
  EXPECT_FALSE(lin::isValidFrameId(255));
}

/* ----------------------------- PID Parity Calculation ----------------------------- */

/** @test P0 parity for ID 0x00: 0 XOR 0 XOR 0 XOR 0 = 0. */
TEST(LinFrameTest, P0ParityId0) { EXPECT_EQ(lin::calculateP0(0x00), 0); }

/** @test P1 parity for ID 0x00: NOT(0 XOR 0 XOR 0 XOR 0) = 1. */
TEST(LinFrameTest, P1ParityId0) { EXPECT_EQ(lin::calculateP1(0x00), 1); }

/** @test PID for ID 0x00 is 0x80 (P0=0, P1=1). */
TEST(LinFrameTest, PidId0) { EXPECT_EQ(lin::calculatePid(0x00), 0x80); }

/** @test PID for ID 0x3C (master request) is 0x3C with parity bits. */
TEST(LinFrameTest, PidMasterRequest) {
  const std::uint8_t PID = lin::calculatePid(lin::Constants::MASTER_REQUEST_ID);
  EXPECT_EQ(lin::extractFrameId(PID), lin::Constants::MASTER_REQUEST_ID);
  EXPECT_TRUE(lin::verifyPidParity(PID));
}

/** @test PID for ID 0x3D (slave response) is 0x3D with parity bits. */
TEST(LinFrameTest, PidSlaveResponse) {
  const std::uint8_t PID = lin::calculatePid(lin::Constants::SLAVE_RESPONSE_ID);
  EXPECT_EQ(lin::extractFrameId(PID), lin::Constants::SLAVE_RESPONSE_ID);
  EXPECT_TRUE(lin::verifyPidParity(PID));
}

/** @test PID parity verification succeeds for valid PIDs. */
TEST(LinFrameTest, VerifyPidParityValid) {
  for (std::uint8_t id = 0; id < 64; ++id) {
    const std::uint8_t PID = lin::calculatePid(id);
    EXPECT_TRUE(lin::verifyPidParity(PID)) << "Failed for ID " << static_cast<int>(id);
  }
}

/** @test PID parity verification fails for corrupted PID. */
TEST(LinFrameTest, VerifyPidParityInvalid) {
  const std::uint8_t PID = lin::calculatePid(0x10);
  const std::uint8_t CORRUPTED = PID ^ 0x40; // Flip P0
  EXPECT_FALSE(lin::verifyPidParity(CORRUPTED));
}

/** @test Extract frame ID from PID. */
TEST(LinFrameTest, ExtractFrameId) {
  EXPECT_EQ(lin::extractFrameId(0x80), 0x00); // ID 0 with P0=0, P1=1
  EXPECT_EQ(lin::extractFrameId(0xFF), 0x3F); // ID 63 with parity
}

/* ----------------------------- Data Length from ID ----------------------------- */

/** @test IDs 0-31 have 2-byte data length. */
TEST(LinFrameTest, DataLengthIds0to31) {
  for (std::uint8_t id = 0; id < 32; ++id) {
    EXPECT_EQ(lin::dataLengthFromId(id), 2) << "Failed for ID " << static_cast<int>(id);
  }
}

/** @test IDs 32-47 have 4-byte data length. */
TEST(LinFrameTest, DataLengthIds32to47) {
  for (std::uint8_t id = 32; id < 48; ++id) {
    EXPECT_EQ(lin::dataLengthFromId(id), 4) << "Failed for ID " << static_cast<int>(id);
  }
}

/** @test IDs 48-63 have 8-byte data length. */
TEST(LinFrameTest, DataLengthIds48to63) {
  for (std::uint8_t id = 48; id < 64; ++id) {
    EXPECT_EQ(lin::dataLengthFromId(id), 8) << "Failed for ID " << static_cast<int>(id);
  }
}

/** @test Diagnostic frames (0x3C, 0x3D) are detected. */
TEST(LinFrameTest, DiagnosticFrameDetection) {
  EXPECT_TRUE(lin::isDiagnosticFrame(0x3C));
  EXPECT_TRUE(lin::isDiagnosticFrame(0x3D));
  EXPECT_FALSE(lin::isDiagnosticFrame(0x00));
  EXPECT_FALSE(lin::isDiagnosticFrame(0x10));
  EXPECT_FALSE(lin::isDiagnosticFrame(0x3F));
}

/* ----------------------------- Checksum Calculation ----------------------------- */

/** @test Classic checksum calculation. */
TEST(LinFrameTest, ClassicChecksum) {
  const std::uint8_t DATA[] = {0x01, 0x02, 0x03};
  const std::uint8_t CHECKSUM = lin::calculateChecksum(DATA, 3, 0x00, lin::ChecksumType::CLASSIC);
  // Sum = 0x01 + 0x02 + 0x03 = 0x06, inverted = 0xF9
  EXPECT_EQ(CHECKSUM, 0xF9);
}

/** @test Enhanced checksum includes PID. */
TEST(LinFrameTest, EnhancedChecksum) {
  const std::uint8_t DATA[] = {0x01, 0x02, 0x03};
  const std::uint8_t PID = 0x80; // ID 0 with parity
  const std::uint8_t CLASSIC = lin::calculateChecksum(DATA, 3, PID, lin::ChecksumType::CLASSIC);
  const std::uint8_t ENHANCED = lin::calculateChecksum(DATA, 3, PID, lin::ChecksumType::ENHANCED);
  // Enhanced includes PID, so result differs from classic
  EXPECT_NE(CLASSIC, ENHANCED);
}

/** @test Checksum with carry propagation. */
TEST(LinFrameTest, ChecksumWithCarry) {
  const std::uint8_t DATA[] = {0xFF, 0x01}; // Sum = 0x100, carry makes 0x01, then 0x01+0x01=0x02
  const std::uint8_t CHECKSUM = lin::calculateChecksum(DATA, 2, 0x00, lin::ChecksumType::CLASSIC);
  // 0xFF + 0x01 = 0x100, carry = 0x01, result = 0x01, inverted = 0xFE
  EXPECT_EQ(CHECKSUM, 0xFE);
}

/** @test Verify checksum succeeds for valid data. */
TEST(LinFrameTest, VerifyChecksumValid) {
  const std::uint8_t DATA[] = {0x01, 0x02, 0x03};
  const std::uint8_t CHECKSUM = lin::calculateChecksum(DATA, 3, 0x00, lin::ChecksumType::CLASSIC);
  EXPECT_TRUE(lin::verifyChecksum(DATA, 3, CHECKSUM, 0x00, lin::ChecksumType::CLASSIC));
}

/** @test Verify checksum fails for corrupted data. */
TEST(LinFrameTest, VerifyChecksumInvalid) {
  const std::uint8_t DATA[] = {0x01, 0x02, 0x03};
  const std::uint8_t WRONG_CHECKSUM = 0x00;
  EXPECT_FALSE(lin::verifyChecksum(DATA, 3, WRONG_CHECKSUM, 0x00, lin::ChecksumType::CLASSIC));
}

/* ----------------------------- FrameBuffer ----------------------------- */

/** @test FrameBuffer starts empty. */
TEST(LinFrameTest, FrameBufferEmpty) {
  lin::FrameBuffer buf;
  EXPECT_EQ(buf.length, 0);
  EXPECT_EQ(buf.remaining(), lin::FrameBuffer::CAPACITY);
}

/** @test FrameBuffer append single byte. */
TEST(LinFrameTest, FrameBufferAppendByte) {
  lin::FrameBuffer buf;
  EXPECT_TRUE(buf.append(0x55));
  EXPECT_EQ(buf.length, 1);
  EXPECT_EQ(buf.data[0], 0x55);
}

/** @test FrameBuffer append multiple bytes. */
TEST(LinFrameTest, FrameBufferAppendBytes) {
  lin::FrameBuffer buf;
  const std::uint8_t DATA[] = {0x01, 0x02, 0x03};
  EXPECT_TRUE(buf.appendBytes(DATA, 3));
  EXPECT_EQ(buf.length, 3);
  EXPECT_EQ(buf.data[0], 0x01);
  EXPECT_EQ(buf.data[1], 0x02);
  EXPECT_EQ(buf.data[2], 0x03);
}

/** @test FrameBuffer reset clears length. */
TEST(LinFrameTest, FrameBufferReset) {
  lin::FrameBuffer buf;
  buf.append(0x55);
  buf.reset();
  EXPECT_EQ(buf.length, 0);
}

/** @test FrameBuffer append fails when full. */
TEST(LinFrameTest, FrameBufferOverflow) {
  lin::FrameBuffer buf;
  for (std::size_t i = 0; i < lin::FrameBuffer::CAPACITY; ++i) {
    EXPECT_TRUE(buf.append(static_cast<std::uint8_t>(i)));
  }
  EXPECT_FALSE(buf.append(0xFF)); // Should fail
}

/* ----------------------------- Frame Building ----------------------------- */

/** @test Build header with valid frame ID. */
TEST(LinFrameTest, BuildHeaderValid) {
  lin::FrameBuffer buf;
  EXPECT_EQ(lin::buildHeader(buf, 0x10), lin::Status::SUCCESS);
  EXPECT_EQ(buf.length, 2);
  EXPECT_EQ(buf.data[0], lin::Constants::SYNC_BYTE);
  EXPECT_EQ(lin::extractFrameId(buf.data[1]), 0x10);
  EXPECT_TRUE(lin::verifyPidParity(buf.data[1]));
}

/** @test Build header rejects invalid frame ID. */
TEST(LinFrameTest, BuildHeaderInvalidId) {
  lin::FrameBuffer buf;
  EXPECT_EQ(lin::buildHeader(buf, 64), lin::Status::ERROR_INVALID_ARG);
}

/** @test Build response with valid data. */
TEST(LinFrameTest, BuildResponseValid) {
  lin::FrameBuffer buf;
  const std::uint8_t DATA[] = {0x01, 0x02, 0x03, 0x04};
  const std::uint8_t PID = lin::calculatePid(0x20);
  EXPECT_EQ(lin::buildResponse(buf, PID, DATA, 4, lin::ChecksumType::ENHANCED),
            lin::Status::SUCCESS);
  EXPECT_EQ(buf.length, 5); // 4 data + 1 checksum
}

/** @test Build response rejects null data. */
TEST(LinFrameTest, BuildResponseNullData) {
  lin::FrameBuffer buf;
  EXPECT_EQ(lin::buildResponse(buf, 0x80, nullptr, 4, lin::ChecksumType::CLASSIC),
            lin::Status::ERROR_INVALID_ARG);
}

/** @test Build response rejects zero-length data. */
TEST(LinFrameTest, BuildResponseZeroLength) {
  lin::FrameBuffer buf;
  const std::uint8_t DATA[] = {0x01};
  EXPECT_EQ(lin::buildResponse(buf, 0x80, DATA, 0, lin::ChecksumType::CLASSIC),
            lin::Status::ERROR_INVALID_ARG);
}

/** @test Build response rejects oversized data. */
TEST(LinFrameTest, BuildResponseOversizedData) {
  lin::FrameBuffer buf;
  std::uint8_t data[16] = {};
  EXPECT_EQ(lin::buildResponse(buf, 0x80, data, 16, lin::ChecksumType::CLASSIC),
            lin::Status::ERROR_INVALID_ARG);
}

/** @test Build complete frame with valid parameters. */
TEST(LinFrameTest, BuildFrameValid) {
  lin::FrameBuffer buf;
  const std::uint8_t DATA[] = {0xAA, 0xBB};
  EXPECT_EQ(lin::buildFrame(buf, 0x00, DATA, 2, lin::ChecksumType::CLASSIC), lin::Status::SUCCESS);
  EXPECT_EQ(buf.length, 5); // sync + PID + 2 data + checksum
  EXPECT_EQ(buf.data[0], lin::Constants::SYNC_BYTE);
  EXPECT_TRUE(lin::verifyPidParity(buf.data[1]));
}

/* ----------------------------- Frame Parsing ----------------------------- */

/** @test Parse valid response. */
TEST(LinFrameTest, ParseResponseValid) {
  // Build a response first
  lin::FrameBuffer buf;
  const std::uint8_t DATA[] = {0x11, 0x22, 0x33, 0x44};
  const std::uint8_t PID = lin::calculatePid(0x20);
  lin::buildResponse(buf, PID, DATA, 4, lin::ChecksumType::ENHANCED);

  // Parse it
  lin::ParsedFrame parsed;
  EXPECT_EQ(lin::parseResponse(buf.data, buf.length, PID, 4, lin::ChecksumType::ENHANCED, parsed),
            lin::Status::SUCCESS);
  EXPECT_EQ(parsed.dataLength, 4);
  EXPECT_TRUE(parsed.checksumValid);
  EXPECT_EQ(parsed.data[0], 0x11);
  EXPECT_EQ(parsed.data[3], 0x44);
}

/** @test Parse response with checksum error. */
TEST(LinFrameTest, ParseResponseChecksumError) {
  lin::FrameBuffer buf;
  const std::uint8_t DATA[] = {0x11, 0x22, 0x33, 0x44};
  const std::uint8_t PID = lin::calculatePid(0x20);
  lin::buildResponse(buf, PID, DATA, 4, lin::ChecksumType::ENHANCED);

  // Corrupt checksum
  buf.data[buf.length - 1] ^= 0xFF;

  lin::ParsedFrame parsed;
  EXPECT_EQ(lin::parseResponse(buf.data, buf.length, PID, 4, lin::ChecksumType::ENHANCED, parsed),
            lin::Status::ERROR_CHECKSUM);
}

/** @test Parse complete frame with valid data. */
TEST(LinFrameTest, ParseFrameValid) {
  // Build a complete frame
  lin::FrameBuffer buf;
  const std::uint8_t DATA[] = {0xDE, 0xAD};
  lin::buildFrame(buf, 0x10, DATA, 2, lin::ChecksumType::CLASSIC);

  // Parse it
  lin::ParsedFrame parsed;
  EXPECT_EQ(lin::parseFrame(buf.data, buf.length, lin::ChecksumType::CLASSIC, parsed),
            lin::Status::SUCCESS);
  EXPECT_EQ(parsed.frameId, 0x10);
  EXPECT_EQ(parsed.dataLength, 2);
  EXPECT_TRUE(parsed.parityValid);
  EXPECT_TRUE(parsed.checksumValid);
}

/** @test Parse frame with sync error. */
TEST(LinFrameTest, ParseFrameSyncError) {
  lin::FrameBuffer buf;
  const std::uint8_t DATA[] = {0xDE, 0xAD};
  lin::buildFrame(buf, 0x10, DATA, 2, lin::ChecksumType::CLASSIC);

  // Corrupt sync byte
  buf.data[0] = 0x00;

  lin::ParsedFrame parsed;
  EXPECT_EQ(lin::parseFrame(buf.data, buf.length, lin::ChecksumType::CLASSIC, parsed),
            lin::Status::ERROR_SYNC);
}

/** @test Parse frame with parity error. */
TEST(LinFrameTest, ParseFrameParityError) {
  lin::FrameBuffer buf;
  const std::uint8_t DATA[] = {0xDE, 0xAD};
  lin::buildFrame(buf, 0x10, DATA, 2, lin::ChecksumType::CLASSIC);

  // Corrupt PID parity
  buf.data[1] ^= 0x80;

  lin::ParsedFrame parsed;
  EXPECT_EQ(lin::parseFrame(buf.data, buf.length, lin::ChecksumType::CLASSIC, parsed),
            lin::Status::ERROR_PARITY);
}

/** @test Parse null frame returns error. */
TEST(LinFrameTest, ParseFrameNull) {
  lin::ParsedFrame parsed;
  EXPECT_EQ(lin::parseFrame(nullptr, 10, lin::ChecksumType::CLASSIC, parsed),
            lin::Status::ERROR_INVALID_ARG);
}

/** @test Parse too-short frame returns error. */
TEST(LinFrameTest, ParseFrameTooShort) {
  const std::uint8_t DATA[] = {0x55, 0x80}; // Just sync + PID, no data/checksum
  lin::ParsedFrame parsed;
  EXPECT_EQ(lin::parseFrame(DATA, 2, lin::ChecksumType::CLASSIC, parsed), lin::Status::ERROR_FRAME);
}

/* ----------------------------- Round-Trip Tests ----------------------------- */

/** @test Build and parse frame round-trip for all frame IDs. */
TEST(LinFrameTest, BuildParseRoundTrip) {
  for (std::uint8_t id = 0; id < 64; ++id) {
    lin::FrameBuffer buf;
    const std::size_t DATA_LEN = lin::dataLengthFromId(id);
    std::uint8_t data[8];
    for (std::size_t i = 0; i < DATA_LEN; ++i) {
      data[i] = static_cast<std::uint8_t>(id + i);
    }

    EXPECT_EQ(lin::buildFrame(buf, id, data, DATA_LEN, lin::ChecksumType::ENHANCED),
              lin::Status::SUCCESS)
        << "Build failed for ID " << static_cast<int>(id);

    lin::ParsedFrame parsed;
    EXPECT_EQ(lin::parseFrame(buf.data, buf.length, lin::ChecksumType::ENHANCED, parsed),
              lin::Status::SUCCESS)
        << "Parse failed for ID " << static_cast<int>(id);

    EXPECT_EQ(parsed.frameId, id);
    EXPECT_EQ(parsed.dataLength, DATA_LEN);
    EXPECT_TRUE(parsed.parityValid);
    EXPECT_TRUE(parsed.checksumValid);
  }
}
