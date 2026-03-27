/**
 * @file AprotoTypes_uTest.cpp
 * @brief Unit tests for APROTO type definitions.
 *
 * Tests structure sizes, constants, and helper functions.
 */

#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoTypes.hpp"

#include <gtest/gtest.h>

using system_core::protocols::aproto::AckPayload;
using system_core::protocols::aproto::APROTO_ACK_PAYLOAD_SIZE;
using system_core::protocols::aproto::APROTO_AUTH_TAG_SIZE;
using system_core::protocols::aproto::APROTO_CRC_SIZE;
using system_core::protocols::aproto::APROTO_CRYPTO_META_SIZE;
using system_core::protocols::aproto::APROTO_HEADER_SIZE;
using system_core::protocols::aproto::APROTO_MAGIC;
using system_core::protocols::aproto::APROTO_MAX_PAYLOAD;
using system_core::protocols::aproto::APROTO_NONCE_SIZE;
using system_core::protocols::aproto::APROTO_VERSION;
using system_core::protocols::aproto::AprotoFlags;
using system_core::protocols::aproto::AprotoHeader;
using system_core::protocols::aproto::byteToFlags;
using system_core::protocols::aproto::CryptoMeta;
using system_core::protocols::aproto::flagsToByte;
using system_core::protocols::aproto::makeFlags;
using system_core::protocols::aproto::packetSize;
using system_core::protocols::aproto::SystemOpcode;

/* ----------------------------- Constants Tests ----------------------------- */

/** @test Magic constant is "AP" in little-endian. */
TEST(AprotoConstantsTest, MagicValue) {
  // 'P' = 0x50, 'A' = 0x41 -> little-endian 0x5041
  EXPECT_EQ(APROTO_MAGIC, 0x5041);
}

/** @test Version constant is 1. */
TEST(AprotoConstantsTest, VersionValue) { EXPECT_EQ(APROTO_VERSION, 1); }

/** @test Header size is exactly 14 bytes. */
TEST(AprotoConstantsTest, HeaderSize) { EXPECT_EQ(APROTO_HEADER_SIZE, 14u); }

/** @test CRC size is 4 bytes. */
TEST(AprotoConstantsTest, CrcSize) { EXPECT_EQ(APROTO_CRC_SIZE, 4u); }

/** @test ACK payload size is 8 bytes. */
TEST(AprotoConstantsTest, AckPayloadSize) { EXPECT_EQ(APROTO_ACK_PAYLOAD_SIZE, 8u); }

/** @test Maximum payload is 65535 bytes (16-bit limit). */
TEST(AprotoConstantsTest, MaxPayload) { EXPECT_EQ(APROTO_MAX_PAYLOAD, 65535u); }

/** @test Crypto metadata size is 13 bytes. */
TEST(AprotoConstantsTest, CryptoMetaSize) { EXPECT_EQ(APROTO_CRYPTO_META_SIZE, 13u); }

/** @test Nonce size is 12 bytes (AES-GCM/ChaCha20-Poly1305). */
TEST(AprotoConstantsTest, NonceSize) { EXPECT_EQ(APROTO_NONCE_SIZE, 12u); }

/** @test Auth tag size is 16 bytes. */
TEST(AprotoConstantsTest, AuthTagSize) { EXPECT_EQ(APROTO_AUTH_TAG_SIZE, 16u); }

/* ----------------------------- AprotoFlags Tests ----------------------------- */

/** @test AprotoFlags struct is exactly 1 byte. */
TEST(AprotoFlagsTest, SizeIsOneByte) { EXPECT_EQ(sizeof(AprotoFlags), 1u); }

/** @test makeFlags creates correct flags. */
TEST(AprotoFlagsTest, MakeFlags) {
  AprotoFlags f = makeFlags(false, false, false, false);
  EXPECT_EQ(f.isResponse, 0);
  EXPECT_EQ(f.ackRequested, 0);
  EXPECT_EQ(f.crcPresent, 0);
  EXPECT_EQ(f.encryptedPresent, 0);

  f = makeFlags(true, true, true, true);
  EXPECT_EQ(f.isResponse, 1);
  EXPECT_EQ(f.ackRequested, 1);
  EXPECT_EQ(f.crcPresent, 1);
  EXPECT_EQ(f.encryptedPresent, 1);

  f = makeFlags(true, false, true, false);
  EXPECT_EQ(f.isResponse, 1);
  EXPECT_EQ(f.ackRequested, 0);
  EXPECT_EQ(f.crcPresent, 1);
  EXPECT_EQ(f.encryptedPresent, 0);
}

/** @test makeFlags with default encryption flag. */
TEST(AprotoFlagsTest, MakeFlagsDefaultEncryption) {
  AprotoFlags f = makeFlags(true, true, true);
  EXPECT_EQ(f.encryptedPresent, 0);
}

/** @test flagsToByte and byteToFlags are inverse operations. */
TEST(AprotoFlagsTest, RoundTrip) {
  for (int i = 0; i < 256; ++i) {
    const std::uint8_t b = static_cast<std::uint8_t>(i);
    const AprotoFlags f = byteToFlags(b);
    const std::uint8_t b2 = flagsToByte(f);
    EXPECT_EQ(b, b2) << "Failed at i=" << i;
  }
}

/** @test Flags bit positions are correct. */
TEST(AprotoFlagsTest, BitPositions) {
  // isResponse is bit 1
  AprotoFlags f = byteToFlags(0x02);
  EXPECT_EQ(f.isResponse, 1);
  EXPECT_EQ(f.ackRequested, 0);
  EXPECT_EQ(f.crcPresent, 0);
  EXPECT_EQ(f.encryptedPresent, 0);

  // ackRequested is bit 2
  f = byteToFlags(0x04);
  EXPECT_EQ(f.isResponse, 0);
  EXPECT_EQ(f.ackRequested, 1);
  EXPECT_EQ(f.crcPresent, 0);
  EXPECT_EQ(f.encryptedPresent, 0);

  // crcPresent is bit 3
  f = byteToFlags(0x08);
  EXPECT_EQ(f.isResponse, 0);
  EXPECT_EQ(f.ackRequested, 0);
  EXPECT_EQ(f.crcPresent, 1);
  EXPECT_EQ(f.encryptedPresent, 0);

  // encryptedPresent is bit 4
  f = byteToFlags(0x10);
  EXPECT_EQ(f.isResponse, 0);
  EXPECT_EQ(f.ackRequested, 0);
  EXPECT_EQ(f.crcPresent, 0);
  EXPECT_EQ(f.encryptedPresent, 1);
}

/* ----------------------------- AprotoHeader Tests ----------------------------- */

/** @test AprotoHeader struct is exactly 14 bytes. */
TEST(AprotoHeaderTest, SizeIs14Bytes) {
  EXPECT_EQ(sizeof(AprotoHeader), 14u);
  EXPECT_EQ(sizeof(AprotoHeader), APROTO_HEADER_SIZE);
}

/** @test Default-initialized header has zero values. */
TEST(AprotoHeaderTest, DefaultInit) {
  AprotoHeader hdr{};
  EXPECT_EQ(hdr.magic, 0);
  EXPECT_EQ(hdr.version, 0);
  EXPECT_EQ(hdr.fullUid, 0u);
  EXPECT_EQ(hdr.opcode, 0);
  EXPECT_EQ(hdr.sequence, 0);
  EXPECT_EQ(hdr.payloadLength, 0);
}

/** @test packetSize calculates correct size without CRC. */
TEST(AprotoHeaderTest, PacketSizeNoCrc) {
  AprotoHeader hdr{};
  hdr.payloadLength = 100;
  hdr.flags.crcPresent = 0;

  EXPECT_EQ(packetSize(hdr), APROTO_HEADER_SIZE + 100);
}

/** @test packetSize calculates correct size with CRC. */
TEST(AprotoHeaderTest, PacketSizeWithCrc) {
  AprotoHeader hdr{};
  hdr.payloadLength = 100;
  hdr.flags.crcPresent = 1;

  EXPECT_EQ(packetSize(hdr), APROTO_HEADER_SIZE + 100 + APROTO_CRC_SIZE);
}

/** @test packetSize with zero payload. */
TEST(AprotoHeaderTest, PacketSizeZeroPayload) {
  AprotoHeader hdr{};
  hdr.payloadLength = 0;
  hdr.flags.crcPresent = 0;
  hdr.flags.encryptedPresent = 0;

  EXPECT_EQ(packetSize(hdr), APROTO_HEADER_SIZE);

  hdr.flags.crcPresent = 1;
  EXPECT_EQ(packetSize(hdr), APROTO_HEADER_SIZE + APROTO_CRC_SIZE);
}

/** @test packetSize with encryption. */
TEST(AprotoHeaderTest, PacketSizeEncrypted) {
  AprotoHeader hdr{};
  hdr.payloadLength = 100;
  hdr.flags.encryptedPresent = 1;
  hdr.flags.crcPresent = 0;

  EXPECT_EQ(packetSize(hdr), APROTO_HEADER_SIZE + APROTO_CRYPTO_META_SIZE + 100);

  hdr.flags.crcPresent = 1;
  EXPECT_EQ(packetSize(hdr), APROTO_HEADER_SIZE + APROTO_CRYPTO_META_SIZE + 100 + APROTO_CRC_SIZE);
}

/* ----------------------------- AckPayload Tests ----------------------------- */

/** @test AckPayload struct is exactly 8 bytes. */
TEST(AckPayloadTest, SizeIs8Bytes) {
  EXPECT_EQ(sizeof(AckPayload), 8u);
  EXPECT_EQ(sizeof(AckPayload), APROTO_ACK_PAYLOAD_SIZE);
}

/** @test Default-initialized AckPayload has zero values. */
TEST(AckPayloadTest, DefaultInit) {
  AckPayload ack{};
  EXPECT_EQ(ack.cmdOpcode, 0);
  EXPECT_EQ(ack.cmdSequence, 0);
  EXPECT_EQ(ack.status, 0);
  EXPECT_EQ(ack.reserved[0], 0);
  EXPECT_EQ(ack.reserved[1], 0);
  EXPECT_EQ(ack.reserved[2], 0);
}

/* ----------------------------- SystemOpcode Tests ----------------------------- */

/** @test SystemOpcode values are in reserved range. */
TEST(SystemOpcodeTest, ValuesInReservedRange) {
  EXPECT_EQ(static_cast<std::uint16_t>(SystemOpcode::NOOP), 0x0000);
  EXPECT_EQ(static_cast<std::uint16_t>(SystemOpcode::PING), 0x0001);
  EXPECT_EQ(static_cast<std::uint16_t>(SystemOpcode::GET_STATUS), 0x0002);
  EXPECT_EQ(static_cast<std::uint16_t>(SystemOpcode::RESET), 0x0003);
  EXPECT_EQ(static_cast<std::uint16_t>(SystemOpcode::ACK), 0x00FE);
  EXPECT_EQ(static_cast<std::uint16_t>(SystemOpcode::NAK), 0x00FF);
}

/** @test ACK and NAK are response-only opcodes. */
TEST(SystemOpcodeTest, AckNakAreResponseOnly) {
  // ACK and NAK should be >= 0x00FE (reserved for responses)
  EXPECT_GE(static_cast<std::uint16_t>(SystemOpcode::ACK), 0x00FE);
  EXPECT_GE(static_cast<std::uint16_t>(SystemOpcode::NAK), 0x00FE);
}

/* ----------------------------- CryptoMeta Tests ----------------------------- */

/** @test CryptoMeta struct is exactly 13 bytes. */
TEST(CryptoMetaTest, SizeIs13Bytes) {
  EXPECT_EQ(sizeof(CryptoMeta), 13u);
  EXPECT_EQ(sizeof(CryptoMeta), APROTO_CRYPTO_META_SIZE);
}

/** @test Default-initialized CryptoMeta has zero values. */
TEST(CryptoMetaTest, DefaultInit) {
  CryptoMeta meta{};
  EXPECT_EQ(meta.keyIndex, 0);
  for (std::size_t i = 0; i < APROTO_NONCE_SIZE; ++i) {
    EXPECT_EQ(meta.nonce[i], 0);
  }
}

/** @test CryptoMeta nonce is exactly 12 bytes. */
TEST(CryptoMetaTest, NonceSizeCorrect) {
  CryptoMeta meta{};
  EXPECT_EQ(sizeof(meta.nonce), APROTO_NONCE_SIZE);
}
