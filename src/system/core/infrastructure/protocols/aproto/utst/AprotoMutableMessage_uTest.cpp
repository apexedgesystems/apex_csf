/**
 * @file AprotoMutableMessage_uTest.cpp
 * @brief Unit tests for APROTO mutable message facade.
 *
 * Tests typed packet assembly via MutableAprotoMessageT and factory builders.
 */

#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoMutableMessage.hpp"
#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoCodec.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <vector>

using system_core::protocols::aproto::APROTO_CRC_SIZE;
using system_core::protocols::aproto::APROTO_CRYPTO_META_SIZE;
using system_core::protocols::aproto::APROTO_HEADER_SIZE;
using system_core::protocols::aproto::APROTO_MAGIC;
using system_core::protocols::aproto::APROTO_VERSION;
using system_core::protocols::aproto::AprotoHeader;
using system_core::protocols::aproto::AprotoMsg;
using system_core::protocols::aproto::createPacketView;
using system_core::protocols::aproto::decodeHeader;
using system_core::protocols::aproto::isSuccess;
using system_core::protocols::aproto::MutableAprotoCryptoMeta;
using system_core::protocols::aproto::MutableAprotoHeader;
using system_core::protocols::aproto::MutableAprotoMessageFactory;
using system_core::protocols::aproto::MutableAprotoMessageT;
using system_core::protocols::aproto::PacketView;
using system_core::protocols::aproto::Status;
using system_core::protocols::aproto::validatePacket;

/* ----------------------------- Test Payload Types ----------------------------- */

namespace {

struct TestCmd {
  std::uint16_t param1;
  std::uint32_t param2;
} __attribute__((packed));

static_assert(sizeof(TestCmd) == 6, "TestCmd must be 6 bytes");

struct TlmPoint {
  float value;
  std::uint32_t timestamp;
} __attribute__((packed));

static_assert(sizeof(TlmPoint) == 8, "TlmPoint must be 8 bytes");

} // namespace

/* ----------------------------- pack() ----------------------------- */

/** @test Pack a single-element command and validate via codec. */
TEST(AprotoMutableMessageTest, PackSingleElement) {
  MutableAprotoMessageT<TestCmd, 256> msg;
  msg.hdr.fullUid = 0x010200;
  msg.hdr.opcode = 0x0100;
  msg.hdr.sequence = 42;

  TestCmd cmd{0x1234, 0xDEADBEEF};
  msg.payload = &cmd;
  msg.payloadCount = 1;

  auto result = msg.pack();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->length, APROTO_HEADER_SIZE + sizeof(TestCmd));

  // Validate via codec.
  auto st = validatePacket(result->span());
  EXPECT_TRUE(isSuccess(st));

  // Decode and check fields.
  AprotoHeader hdr{};
  ASSERT_TRUE(isSuccess(decodeHeader(result->span(), hdr)));
  EXPECT_EQ(hdr.fullUid, 0x010200u);
  EXPECT_EQ(hdr.opcode, 0x0100);
  EXPECT_EQ(hdr.sequence, 42);
  EXPECT_EQ(hdr.payloadLength, sizeof(TestCmd));
}

/** @test Pack with CRC and verify packet passes validation. */
TEST(AprotoMutableMessageTest, PackWithCrc) {
  MutableAprotoMessageT<TestCmd, 256> msg;
  msg.hdr.fullUid = 0x020300;
  msg.hdr.opcode = 0x0200;
  msg.hdr.sequence = 7;
  msg.hdr.includeCrc = true;

  TestCmd cmd{0x5678, 0xCAFEBABE};
  msg.payload = &cmd;
  msg.payloadCount = 1;

  auto result = msg.pack();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->length, APROTO_HEADER_SIZE + sizeof(TestCmd) + APROTO_CRC_SIZE);

  // CRC validation via codec.
  auto st = validatePacket(result->span());
  EXPECT_TRUE(isSuccess(st));
}

/** @test Pack as response packet. */
TEST(AprotoMutableMessageTest, PackAsResponse) {
  MutableAprotoMessageT<TlmPoint, 256> msg;
  msg.hdr.fullUid = 0x030400;
  msg.hdr.opcode = 0x0001;
  msg.hdr.sequence = 100;
  msg.hdr.isResponse = true;

  TlmPoint tlm{3.14f, 1000};
  msg.payload = &tlm;
  msg.payloadCount = 1;

  auto result = msg.pack();
  ASSERT_TRUE(result.has_value());

  PacketView view{};
  ASSERT_TRUE(isSuccess(createPacketView(result->span(), view)));
  EXPECT_TRUE(view.isResponse());
  EXPECT_FALSE(view.ackRequested());
}

/* ----------------------------- packInto() ----------------------------- */

/** @test packInto writes to caller-provided buffer. */
TEST(AprotoMutableMessageTest, PackIntoBuffer) {
  MutableAprotoMessageT<TestCmd, 256> msg;
  msg.hdr.fullUid = 0x010200;
  msg.hdr.opcode = 0x0100;
  msg.hdr.sequence = 1;

  TestCmd cmd{0x1111, 0x22222222};
  msg.payload = &cmd;
  msg.payloadCount = 1;

  std::array<std::uint8_t, 256> buf{};
  auto written = msg.packInto(buf.data(), buf.size());
  ASSERT_TRUE(written.has_value());
  EXPECT_EQ(*written, APROTO_HEADER_SIZE + sizeof(TestCmd));

  // Validate via codec.
  auto st = validatePacket({buf.data(), *written});
  EXPECT_TRUE(isSuccess(st));
}

/** @test packInto returns nullopt when buffer is too small. */
TEST(AprotoMutableMessageTest, PackIntoTooSmall) {
  MutableAprotoMessageT<TestCmd, 256> msg;
  msg.hdr.fullUid = 0x010200;
  msg.hdr.opcode = 0x0100;
  msg.hdr.sequence = 1;

  TestCmd cmd{0x1111, 0x22222222};
  msg.payload = &cmd;
  msg.payloadCount = 1;

  std::array<std::uint8_t, 4> tiny{};
  auto written = msg.packInto(tiny.data(), tiny.size());
  EXPECT_FALSE(written.has_value());
}

/** @test packInto returns nullopt for null output pointer. */
TEST(AprotoMutableMessageTest, PackIntoNullBuffer) {
  MutableAprotoMessageT<TestCmd, 256> msg;
  msg.hdr.fullUid = 0x010200;
  msg.hdr.opcode = 0x0100;
  TestCmd cmd{};
  msg.payload = &cmd;
  msg.payloadCount = 1;

  auto written = msg.packInto(nullptr, 256);
  EXPECT_FALSE(written.has_value());
}

/* ----------------------------- requiredSize() ----------------------------- */

/** @test requiredSize returns correct total for plain packet. */
TEST(AprotoMutableMessageTest, RequiredSizePlain) {
  MutableAprotoMessageT<TestCmd, 256> msg;
  TestCmd cmd{};
  msg.payload = &cmd;
  msg.payloadCount = 1;

  EXPECT_EQ(msg.requiredSize(), APROTO_HEADER_SIZE + sizeof(TestCmd));
}

/** @test requiredSize accounts for CRC. */
TEST(AprotoMutableMessageTest, RequiredSizeWithCrc) {
  MutableAprotoMessageT<TestCmd, 256> msg;
  msg.hdr.includeCrc = true;
  TestCmd cmd{};
  msg.payload = &cmd;
  msg.payloadCount = 1;

  EXPECT_EQ(msg.requiredSize(), APROTO_HEADER_SIZE + sizeof(TestCmd) + APROTO_CRC_SIZE);
}

/** @test requiredSize accounts for crypto metadata. */
TEST(AprotoMutableMessageTest, RequiredSizeWithCrypto) {
  MutableAprotoMessageT<TestCmd, 256> msg;
  msg.crypto = MutableAprotoCryptoMeta{};
  TestCmd cmd{};
  msg.payload = &cmd;
  msg.payloadCount = 1;

  EXPECT_EQ(msg.requiredSize(), APROTO_HEADER_SIZE + APROTO_CRYPTO_META_SIZE + sizeof(TestCmd));
}

/* ----------------------------- setPayload ----------------------------- */

/** @test setPayload from span. */
TEST(AprotoMutableMessageTest, SetPayloadFromSpan) {
  MutableAprotoMessageT<std::uint8_t, 256> msg;
  std::array<std::uint8_t, 4> data = {0xAA, 0xBB, 0xCC, 0xDD};

  msg.setPayload(apex::compat::rospan<std::uint8_t>{data.data(), data.size()});
  EXPECT_EQ(msg.payloadCount, 4u);
  EXPECT_EQ(msg.payload, data.data());
}

/** @test setPayload from pointer and count. */
TEST(AprotoMutableMessageTest, SetPayloadFromPtrCount) {
  MutableAprotoMessageT<TestCmd, 256> msg;
  TestCmd cmds[2] = {{0x11, 0x22}, {0x33, 0x44}};

  msg.setPayload(cmds, 2);
  EXPECT_EQ(msg.payloadCount, 2u);
  EXPECT_EQ(msg.payload, cmds);
}

/* ----------------------------- Factory ----------------------------- */

/** @test Factory build from single instance. */
TEST(AprotoMutableMessageTest, FactorySingleInstance) {
  TestCmd cmd{0xAAAA, 0xBBBBBBBB};
  auto opt = MutableAprotoMessageFactory::build<TestCmd, 256>(0x010200, 0x0100, 1, cmd);
  ASSERT_TRUE(opt.has_value());

  auto packed = opt->pack();
  ASSERT_TRUE(packed.has_value());

  auto st = validatePacket(packed->span());
  EXPECT_TRUE(isSuccess(st));
}

/** @test Factory build from span. */
TEST(AprotoMutableMessageTest, FactoryFromSpan) {
  std::array<std::uint8_t, 8> data = {1, 2, 3, 4, 5, 6, 7, 8};
  apex::compat::rospan<std::uint8_t> span{data.data(), data.size()};

  auto opt = MutableAprotoMessageFactory::build<std::uint8_t, 256>(0x020300, 0x0200, 5, span);
  ASSERT_TRUE(opt.has_value());

  auto packed = opt->pack();
  ASSERT_TRUE(packed.has_value());
  EXPECT_EQ(packed->length, APROTO_HEADER_SIZE + 8u);
}

/** @test Factory build from pointer + count. */
TEST(AprotoMutableMessageTest, FactoryFromPtrCount) {
  TestCmd cmds[3] = {{1, 2}, {3, 4}, {5, 6}};
  auto opt = MutableAprotoMessageFactory::build<TestCmd, 256>(0x030400, 0x0300, 10, cmds, 3);
  ASSERT_TRUE(opt.has_value());

  auto packed = opt->pack();
  ASSERT_TRUE(packed.has_value());
  EXPECT_EQ(packed->length, APROTO_HEADER_SIZE + 3 * sizeof(TestCmd));
}

/** @test Factory rejects empty span. */
TEST(AprotoMutableMessageTest, FactoryRejectsEmptySpan) {
  apex::compat::rospan<std::uint8_t> empty{};
  auto opt = MutableAprotoMessageFactory::build<std::uint8_t, 256>(0x010200, 0x0100, 1, empty);
  EXPECT_FALSE(opt.has_value());
}

/** @test Factory rejects null pointer with nonzero count. */
TEST(AprotoMutableMessageTest, FactoryRejectsNullPtr) {
  auto opt = MutableAprotoMessageFactory::build<TestCmd, 256>(
      0x010200, 0x0100, 1, static_cast<const TestCmd*>(nullptr), 2);
  EXPECT_FALSE(opt.has_value());
}

/** @test Factory with CRC flag produces valid CRC packet. */
TEST(AprotoMutableMessageTest, FactoryWithCrc) {
  TestCmd cmd{0x1234, 0x56789ABC};
  auto opt =
      MutableAprotoMessageFactory::build<TestCmd, 256>(0x010200, 0x0100, 1, cmd, false, true, true);
  ASSERT_TRUE(opt.has_value());

  auto packed = opt->pack();
  ASSERT_TRUE(packed.has_value());

  auto st = validatePacket(packed->span());
  EXPECT_TRUE(isSuccess(st));

  PacketView view{};
  ASSERT_TRUE(isSuccess(createPacketView(packed->span(), view)));
  EXPECT_TRUE(view.hasCrc());
  EXPECT_TRUE(view.ackRequested());
}

/* ----------------------------- Crypto Metadata ----------------------------- */

/** @test Pack with crypto metadata sets encrypted flag and includes metadata. */
TEST(AprotoMutableMessageTest, PackWithCryptoMeta) {
  MutableAprotoMessageT<TestCmd, 256> msg;
  msg.hdr.fullUid = 0x010200;
  msg.hdr.opcode = 0x0100;
  msg.hdr.sequence = 1;

  MutableAprotoCryptoMeta crypto;
  crypto.keyIndex = 3;
  std::array<std::uint8_t, 12> nonce = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  EXPECT_TRUE(crypto.setNonce({nonce.data(), nonce.size()}));
  msg.crypto = crypto;

  TestCmd cmd{0xAAAA, 0xBBBBBBBB};
  msg.payload = &cmd;
  msg.payloadCount = 1;

  auto result = msg.pack();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->length, APROTO_HEADER_SIZE + APROTO_CRYPTO_META_SIZE + sizeof(TestCmd));

  // Verify encrypted flag is set in header.
  AprotoHeader hdr{};
  ASSERT_TRUE(isSuccess(decodeHeader(result->span(), hdr)));
  EXPECT_EQ(hdr.flags.encryptedPresent, 1);

  // Verify crypto metadata bytes.
  const std::uint8_t* cryptoBytes = result->data.data() + APROTO_HEADER_SIZE;
  EXPECT_EQ(cryptoBytes[0], 3); // keyIndex
  for (std::size_t i = 0; i < 12; ++i) {
    EXPECT_EQ(cryptoBytes[1 + i], nonce[i]);
  }
}

/** @test CryptoMeta setNonce rejects wrong size. */
TEST(AprotoMutableMessageTest, CryptoMetaRejectsWrongNonceSize) {
  MutableAprotoCryptoMeta crypto;
  std::array<std::uint8_t, 8> shortNonce = {1, 2, 3, 4, 5, 6, 7, 8};
  EXPECT_FALSE(crypto.setNonce({shortNonce.data(), shortNonce.size()}));
}

/* ----------------------------- AprotoMsg ----------------------------- */

/** @test AprotoMsg capacity matches template parameter. */
TEST(AprotoMutableMessageTest, AprotoMsgCapacity) {
  AprotoMsg<512> msg;
  EXPECT_EQ(msg.capacity(), 512u);
  EXPECT_EQ(msg.length, 0u);
  EXPECT_TRUE(msg.span().empty());
}
