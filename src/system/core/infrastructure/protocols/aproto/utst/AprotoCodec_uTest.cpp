/**
 * @file AprotoCodec_uTest.cpp
 * @brief Unit tests for APROTO encode/decode functions.
 *
 * Tests encoding, decoding, CRC validation, and packet views.
 */

#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoCodec.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <vector>

using system_core::protocols::aproto::AckPayload;
using system_core::protocols::aproto::appendCrc;
using system_core::protocols::aproto::APROTO_ACK_PAYLOAD_SIZE;
using system_core::protocols::aproto::APROTO_CRC_SIZE;
using system_core::protocols::aproto::APROTO_CRYPTO_META_SIZE;
using system_core::protocols::aproto::APROTO_HEADER_SIZE;
using system_core::protocols::aproto::APROTO_MAGIC;
using system_core::protocols::aproto::APROTO_VERSION;
using system_core::protocols::aproto::AprotoHeader;
using system_core::protocols::aproto::buildHeader;
using system_core::protocols::aproto::computeCrc;
using system_core::protocols::aproto::createPacketView;
using system_core::protocols::aproto::CryptoMeta;
using system_core::protocols::aproto::decodeHeader;
using system_core::protocols::aproto::encodeAckNak;
using system_core::protocols::aproto::encodeHeader;
using system_core::protocols::aproto::encodePacket;
using system_core::protocols::aproto::getCryptoMeta;
using system_core::protocols::aproto::getPayload;
using system_core::protocols::aproto::isSuccess;
using system_core::protocols::aproto::packetSize;
using system_core::protocols::aproto::PacketView;
using system_core::protocols::aproto::Status;
using system_core::protocols::aproto::SystemOpcode;
using system_core::protocols::aproto::validatePacket;

/* ----------------------------- buildHeader Tests ----------------------------- */

/** @test buildHeader populates all fields correctly. */
TEST(AprotoCodecTest, BuildHeaderFields) {
  AprotoHeader hdr = buildHeader(0x00010002, 0x1234, 0x5678, 100, true, true, true);

  EXPECT_EQ(hdr.magic, APROTO_MAGIC);
  EXPECT_EQ(hdr.version, APROTO_VERSION);
  EXPECT_EQ(hdr.fullUid, 0x00010002u);
  EXPECT_EQ(hdr.opcode, 0x1234);
  EXPECT_EQ(hdr.sequence, 0x5678);
  EXPECT_EQ(hdr.payloadLength, 100);
  EXPECT_EQ(hdr.flags.isResponse, 1);
  EXPECT_EQ(hdr.flags.ackRequested, 1);
  EXPECT_EQ(hdr.flags.crcPresent, 1);
}

/** @test buildHeader with defaults. */
TEST(AprotoCodecTest, BuildHeaderDefaults) {
  AprotoHeader hdr = buildHeader(0x00010002, 0x1234, 0x5678, 100);

  EXPECT_EQ(hdr.flags.isResponse, 0);
  EXPECT_EQ(hdr.flags.ackRequested, 0);
  EXPECT_EQ(hdr.flags.crcPresent, 0);
}

/* ----------------------------- encodeHeader Tests ----------------------------- */

/** @test encodeHeader writes correct bytes. */
TEST(AprotoCodecTest, EncodeHeader) {
  AprotoHeader hdr = buildHeader(0x00010002, 0x1234, 0x5678, 100);
  std::array<std::uint8_t, APROTO_HEADER_SIZE> buf{};

  Status st = encodeHeader(hdr, apex::compat::mutable_bytes_span{buf.data(), buf.size()});
  EXPECT_TRUE(isSuccess(st));

  // Verify magic
  EXPECT_EQ(buf[0], 0x41); // 'A'
  EXPECT_EQ(buf[1], 0x50); // 'P'

  // Verify version
  EXPECT_EQ(buf[2], APROTO_VERSION);
}

/** @test encodeHeader fails on small buffer. */
TEST(AprotoCodecTest, EncodeHeaderBufferTooSmall) {
  AprotoHeader hdr = buildHeader(0, 0, 0, 0);
  std::array<std::uint8_t, 10> buf{};

  Status st = encodeHeader(hdr, apex::compat::mutable_bytes_span{buf.data(), buf.size()});
  EXPECT_EQ(st, Status::ERROR_BUFFER_TOO_SMALL);
}

/* ----------------------------- decodeHeader Tests ----------------------------- */

/** @test decodeHeader parses valid header. */
TEST(AprotoCodecTest, DecodeHeaderValid) {
  AprotoHeader orig = buildHeader(0x00010002, 0x1234, 0x5678, 100, true, true, true);
  std::array<std::uint8_t, APROTO_HEADER_SIZE> buf{};
  [[maybe_unused]] Status enc_st =
      encodeHeader(orig, apex::compat::mutable_bytes_span{buf.data(), buf.size()});

  AprotoHeader decoded{};
  Status st = decodeHeader(apex::compat::rospan<std::uint8_t>{buf.data(), buf.size()}, decoded);
  EXPECT_TRUE(isSuccess(st));

  EXPECT_EQ(decoded.magic, APROTO_MAGIC);
  EXPECT_EQ(decoded.version, APROTO_VERSION);
  EXPECT_EQ(decoded.fullUid, orig.fullUid);
  EXPECT_EQ(decoded.opcode, orig.opcode);
  EXPECT_EQ(decoded.sequence, orig.sequence);
  EXPECT_EQ(decoded.payloadLength, orig.payloadLength);
}

/** @test decodeHeader fails on invalid magic. */
TEST(AprotoCodecTest, DecodeHeaderInvalidMagic) {
  std::array<std::uint8_t, APROTO_HEADER_SIZE> buf{};
  buf[0] = 0xFF;
  buf[1] = 0xFF;

  AprotoHeader hdr{};
  Status st = decodeHeader(apex::compat::rospan<std::uint8_t>{buf.data(), buf.size()}, hdr);
  EXPECT_EQ(st, Status::ERROR_INVALID_MAGIC);
}

/** @test decodeHeader fails on invalid version. */
TEST(AprotoCodecTest, DecodeHeaderInvalidVersion) {
  AprotoHeader orig = buildHeader(0, 0, 0, 0);
  std::array<std::uint8_t, APROTO_HEADER_SIZE> buf{};
  [[maybe_unused]] Status enc_st =
      encodeHeader(orig, apex::compat::mutable_bytes_span{buf.data(), buf.size()});
  buf[2] = 99; // Invalid version

  AprotoHeader hdr{};
  Status st = decodeHeader(apex::compat::rospan<std::uint8_t>{buf.data(), buf.size()}, hdr);
  EXPECT_EQ(st, Status::ERROR_INVALID_VERSION);
}

/** @test decodeHeader fails on buffer too small. */
TEST(AprotoCodecTest, DecodeHeaderIncomplete) {
  std::array<std::uint8_t, 5> buf{};

  AprotoHeader hdr{};
  Status st = decodeHeader(apex::compat::rospan<std::uint8_t>{buf.data(), buf.size()}, hdr);
  EXPECT_EQ(st, Status::ERROR_INCOMPLETE);
}

/* ----------------------------- encodePacket Tests ----------------------------- */

/** @test encodePacket round-trip without CRC. */
TEST(AprotoCodecTest, EncodePacketNoCrc) {
  std::vector<std::uint8_t> payload{0xDE, 0xAD, 0xBE, 0xEF};
  AprotoHeader hdr =
      buildHeader(0x00010002, 0x1234, 0x5678, static_cast<std::uint16_t>(payload.size()));

  std::vector<std::uint8_t> buf(packetSize(hdr));
  std::size_t written = 0;

  Status st = encodePacket(hdr, apex::compat::rospan<std::uint8_t>{payload.data(), payload.size()},
                           apex::compat::mutable_bytes_span{buf.data(), buf.size()}, written);

  EXPECT_TRUE(isSuccess(st));
  EXPECT_EQ(written, APROTO_HEADER_SIZE + payload.size());
}

/** @test encodePacket round-trip with CRC. */
TEST(AprotoCodecTest, EncodePacketWithCrc) {
  std::vector<std::uint8_t> payload{0xDE, 0xAD, 0xBE, 0xEF};
  AprotoHeader hdr = buildHeader(0x00010002, 0x1234, 0x5678,
                                 static_cast<std::uint16_t>(payload.size()), false, false, true);

  std::vector<std::uint8_t> buf(packetSize(hdr));
  std::size_t written = 0;

  Status st = encodePacket(hdr, apex::compat::rospan<std::uint8_t>{payload.data(), payload.size()},
                           apex::compat::mutable_bytes_span{buf.data(), buf.size()}, written);

  EXPECT_TRUE(isSuccess(st));
  EXPECT_EQ(written, APROTO_HEADER_SIZE + payload.size() + APROTO_CRC_SIZE);
}

/** @test encodePacket fails on payload size mismatch. */
TEST(AprotoCodecTest, EncodePacketPayloadMismatch) {
  std::vector<std::uint8_t> payload{0xDE, 0xAD};
  AprotoHeader hdr = buildHeader(0, 0, 0, 100); // Wrong size

  std::vector<std::uint8_t> buf(256);
  std::size_t written = 0;

  Status st = encodePacket(hdr, apex::compat::rospan<std::uint8_t>{payload.data(), payload.size()},
                           apex::compat::mutable_bytes_span{buf.data(), buf.size()}, written);

  EXPECT_EQ(st, Status::ERROR_PAYLOAD_TOO_LARGE);
}

/* ----------------------------- validatePacket Tests ----------------------------- */

/** @test validatePacket succeeds for valid packet. */
TEST(AprotoCodecTest, ValidatePacketValid) {
  std::vector<std::uint8_t> payload{0xDE, 0xAD, 0xBE, 0xEF};
  AprotoHeader hdr =
      buildHeader(0x00010002, 0x1234, 0x5678, static_cast<std::uint16_t>(payload.size()));

  std::vector<std::uint8_t> buf(packetSize(hdr));
  std::size_t written = 0;
  [[maybe_unused]] Status enc_st =
      encodePacket(hdr, apex::compat::rospan<std::uint8_t>{payload.data(), payload.size()},
                   apex::compat::mutable_bytes_span{buf.data(), buf.size()}, written);

  Status st = validatePacket(apex::compat::rospan<std::uint8_t>{buf.data(), written});
  EXPECT_TRUE(isSuccess(st));
}

/** @test validatePacket succeeds with valid CRC. */
TEST(AprotoCodecTest, ValidatePacketWithCrc) {
  std::vector<std::uint8_t> payload{0xDE, 0xAD, 0xBE, 0xEF};
  AprotoHeader hdr = buildHeader(0x00010002, 0x1234, 0x5678,
                                 static_cast<std::uint16_t>(payload.size()), false, false, true);

  std::vector<std::uint8_t> buf(packetSize(hdr));
  std::size_t written = 0;
  [[maybe_unused]] Status enc_st =
      encodePacket(hdr, apex::compat::rospan<std::uint8_t>{payload.data(), payload.size()},
                   apex::compat::mutable_bytes_span{buf.data(), buf.size()}, written);

  Status st = validatePacket(apex::compat::rospan<std::uint8_t>{buf.data(), written});
  EXPECT_TRUE(isSuccess(st));
}

/** @test validatePacket fails with corrupted CRC. */
TEST(AprotoCodecTest, ValidatePacketCrcMismatch) {
  std::vector<std::uint8_t> payload{0xDE, 0xAD, 0xBE, 0xEF};
  AprotoHeader hdr = buildHeader(0x00010002, 0x1234, 0x5678,
                                 static_cast<std::uint16_t>(payload.size()), false, false, true);

  std::vector<std::uint8_t> buf(packetSize(hdr));
  std::size_t written = 0;
  [[maybe_unused]] Status enc_st =
      encodePacket(hdr, apex::compat::rospan<std::uint8_t>{payload.data(), payload.size()},
                   apex::compat::mutable_bytes_span{buf.data(), buf.size()}, written);

  // Corrupt CRC
  buf[written - 1] ^= 0xFF;

  Status st = validatePacket(apex::compat::rospan<std::uint8_t>{buf.data(), written});
  EXPECT_EQ(st, Status::ERROR_CRC_MISMATCH);
}

/** @test validatePacket fails with truncated payload. */
TEST(AprotoCodecTest, ValidatePacketTruncated) {
  std::vector<std::uint8_t> payload{0xDE, 0xAD, 0xBE, 0xEF};
  AprotoHeader hdr =
      buildHeader(0x00010002, 0x1234, 0x5678, static_cast<std::uint16_t>(payload.size()));

  std::vector<std::uint8_t> buf(packetSize(hdr));
  std::size_t written = 0;
  [[maybe_unused]] Status enc_st =
      encodePacket(hdr, apex::compat::rospan<std::uint8_t>{payload.data(), payload.size()},
                   apex::compat::mutable_bytes_span{buf.data(), buf.size()}, written);

  // Truncate packet
  Status st = validatePacket(apex::compat::rospan<std::uint8_t>{buf.data(), written - 2});
  EXPECT_EQ(st, Status::ERROR_PAYLOAD_TRUNCATED);
}

/* ----------------------------- getPayload Tests ----------------------------- */

/** @test getPayload returns correct payload. */
TEST(AprotoCodecTest, GetPayload) {
  std::vector<std::uint8_t> payload{0xDE, 0xAD, 0xBE, 0xEF};
  AprotoHeader hdr =
      buildHeader(0x00010002, 0x1234, 0x5678, static_cast<std::uint16_t>(payload.size()));

  std::vector<std::uint8_t> buf(packetSize(hdr));
  std::size_t written = 0;
  [[maybe_unused]] Status enc_st =
      encodePacket(hdr, apex::compat::rospan<std::uint8_t>{payload.data(), payload.size()},
                   apex::compat::mutable_bytes_span{buf.data(), buf.size()}, written);

  auto result = getPayload(apex::compat::rospan<std::uint8_t>{buf.data(), written});
  EXPECT_EQ(result.size(), payload.size());
  EXPECT_EQ(std::memcmp(result.data(), payload.data(), payload.size()), 0);
}

/** @test getPayload returns empty for zero-length payload. */
TEST(AprotoCodecTest, GetPayloadEmpty) {
  AprotoHeader hdr = buildHeader(0, 0, 0, 0);

  std::vector<std::uint8_t> buf(packetSize(hdr));
  std::size_t written = 0;
  [[maybe_unused]] Status enc_st =
      encodePacket(hdr, apex::compat::rospan<std::uint8_t>{},
                   apex::compat::mutable_bytes_span{buf.data(), buf.size()}, written);

  auto result = getPayload(apex::compat::rospan<std::uint8_t>{buf.data(), written});
  EXPECT_TRUE(result.empty());
}

/* ----------------------------- encodeAckNak Tests ----------------------------- */

/** @test encodeAckNak creates ACK for success. */
TEST(AprotoCodecTest, EncodeAck) {
  AprotoHeader cmdHdr = buildHeader(0x00010002, 0x1234, 0x5678, 0, false, true, false);

  std::vector<std::uint8_t> buf(64);
  std::size_t written = 0;

  Status st =
      encodeAckNak(cmdHdr, 0, apex::compat::mutable_bytes_span{buf.data(), buf.size()}, written);
  EXPECT_TRUE(isSuccess(st));

  // Decode and verify
  AprotoHeader respHdr{};
  [[maybe_unused]] Status dec_st =
      decodeHeader(apex::compat::rospan<std::uint8_t>{buf.data(), written}, respHdr);
  EXPECT_EQ(respHdr.opcode, static_cast<std::uint16_t>(SystemOpcode::ACK));
  EXPECT_EQ(respHdr.flags.isResponse, 1);
}

/** @test encodeAckNak creates NAK for error. */
TEST(AprotoCodecTest, EncodeNak) {
  AprotoHeader cmdHdr = buildHeader(0x00010002, 0x1234, 0x5678, 0, false, true, false);

  std::vector<std::uint8_t> buf(64);
  std::size_t written = 0;

  Status st =
      encodeAckNak(cmdHdr, 1, apex::compat::mutable_bytes_span{buf.data(), buf.size()}, written);
  EXPECT_TRUE(isSuccess(st));

  // Decode and verify
  AprotoHeader respHdr{};
  [[maybe_unused]] Status dec_st =
      decodeHeader(apex::compat::rospan<std::uint8_t>{buf.data(), written}, respHdr);
  EXPECT_EQ(respHdr.opcode, static_cast<std::uint16_t>(SystemOpcode::NAK));
}

/* ----------------------------- computeCrc Tests ----------------------------- */

/** @test computeCrc is deterministic. */
TEST(AprotoCodecTest, ComputeCrcDeterministic) {
  std::vector<std::uint8_t> data{0x01, 0x02, 0x03, 0x04};

  std::uint32_t crc1 = computeCrc(apex::compat::rospan<std::uint8_t>{data.data(), data.size()});
  std::uint32_t crc2 = computeCrc(apex::compat::rospan<std::uint8_t>{data.data(), data.size()});

  EXPECT_EQ(crc1, crc2);
}

/** @test computeCrc differs for different data. */
TEST(AprotoCodecTest, ComputeCrcDiffers) {
  std::vector<std::uint8_t> data1{0x01, 0x02, 0x03, 0x04};
  std::vector<std::uint8_t> data2{0x01, 0x02, 0x03, 0x05};

  std::uint32_t crc1 = computeCrc(apex::compat::rospan<std::uint8_t>{data1.data(), data1.size()});
  std::uint32_t crc2 = computeCrc(apex::compat::rospan<std::uint8_t>{data2.data(), data2.size()});

  EXPECT_NE(crc1, crc2);
}

/* ----------------------------- PacketView Tests ----------------------------- */

/** @test createPacketView for valid packet. */
TEST(AprotoCodecTest, CreatePacketView) {
  std::vector<std::uint8_t> payload{0xDE, 0xAD, 0xBE, 0xEF};
  AprotoHeader hdr = buildHeader(0x00010002, 0x1234, 0x5678,
                                 static_cast<std::uint16_t>(payload.size()), true, true, true);

  std::vector<std::uint8_t> buf(packetSize(hdr));
  std::size_t written = 0;
  [[maybe_unused]] Status enc_st =
      encodePacket(hdr, apex::compat::rospan<std::uint8_t>{payload.data(), payload.size()},
                   apex::compat::mutable_bytes_span{buf.data(), buf.size()}, written);

  PacketView view{};
  Status st = createPacketView(apex::compat::rospan<std::uint8_t>{buf.data(), written}, view);
  EXPECT_TRUE(isSuccess(st));

  EXPECT_EQ(view.header.fullUid, hdr.fullUid);
  EXPECT_EQ(view.header.opcode, hdr.opcode);
  EXPECT_TRUE(view.hasCrc());
  EXPECT_TRUE(view.isResponse());
  EXPECT_TRUE(view.ackRequested());
  EXPECT_EQ(view.payload.size(), payload.size());
}

/* ----------------------------- getCryptoMeta Tests ----------------------------- */

/** @test getCryptoMeta fails for non-encrypted packet. */
TEST(AprotoCodecTest, GetCryptoMetaNotEncrypted) {
  AprotoHeader hdr = buildHeader(0, 0, 0, 0);
  std::vector<std::uint8_t> buf(packetSize(hdr));
  std::size_t written = 0;
  [[maybe_unused]] Status enc_st =
      encodePacket(hdr, apex::compat::rospan<std::uint8_t>{},
                   apex::compat::mutable_bytes_span{buf.data(), buf.size()}, written);

  CryptoMeta meta{};
  Status st = getCryptoMeta(apex::compat::rospan<std::uint8_t>{buf.data(), written}, meta);
  EXPECT_EQ(st, Status::ERROR_MISSING_CRYPTO);
}

/* ----------------------------- appendCrc Tests ----------------------------- */

/** @test appendCrc appends CRC consistent with computeCrc. */
TEST(AprotoCodecTest, AppendCrcMatchesComputeCrc) {
  std::array<std::uint8_t, 8> data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  std::array<std::uint8_t, 12> buf{};

  // Copy data into buf, then append CRC after it
  std::memcpy(buf.data(), data.data(), data.size());
  const Status st =
      appendCrc(apex::compat::rospan<std::uint8_t>{data.data(), data.size()},
                apex::compat::mutable_bytes_span{buf.data(), buf.size()}, data.size());
  EXPECT_EQ(st, Status::SUCCESS);

  // Verify appended CRC matches computeCrc result (little-endian)
  const std::uint32_t expected =
      computeCrc(apex::compat::rospan<std::uint8_t>{data.data(), data.size()});
  std::uint32_t stored = 0;
  stored |= static_cast<std::uint32_t>(buf[8]);
  stored |= static_cast<std::uint32_t>(buf[9]) << 8;
  stored |= static_cast<std::uint32_t>(buf[10]) << 16;
  stored |= static_cast<std::uint32_t>(buf[11]) << 24;
  EXPECT_EQ(stored, expected);
}

/** @test appendCrc returns ERROR_BUFFER_TOO_SMALL when buffer cannot fit CRC. */
TEST(AprotoCodecTest, AppendCrcBufferTooSmall) {
  std::array<std::uint8_t, 4> data = {0xAA, 0xBB, 0xCC, 0xDD};
  std::array<std::uint8_t, 4> buf{}; // No room for CRC after offset=2

  const Status st = appendCrc(apex::compat::rospan<std::uint8_t>{data.data(), data.size()},
                              apex::compat::mutable_bytes_span{buf.data(), buf.size()},
                              2); // offset=2, needs 4 bytes, only 2 available
  EXPECT_EQ(st, Status::ERROR_BUFFER_TOO_SMALL);
}
