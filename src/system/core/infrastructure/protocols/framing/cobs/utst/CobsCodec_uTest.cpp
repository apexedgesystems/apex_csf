/**
 * @file CobsCodec_uTest.cpp
 * @brief Unit tests for the CobsCodec templated wrapper.
 *
 * Tests the convenience wrapper around the raw COBS encode/decode API.
 * Verifies buffer ownership, config propagation, and encode/decode roundtrips
 * at various template-parameterized sizes.
 */

#include "src/system/core/infrastructure/protocols/framing/cobs/inc/CobsCodec.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

using apex::protocols::cobs::CobsCodec;
using apex::protocols::cobs::DELIMITER;
using apex::protocols::cobs::Status;
using Span = apex::compat::bytes_span;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verify default codec has correct config maxFrameSize. */
TEST(CobsCodec, DefaultConfigMaxFrameSize) {
  CobsCodec<128> codec;
  EXPECT_EQ(codec.config().maxFrameSize, 128U);
}

/** @test Verify default codec uses DEFAULT_MAX_FRAME_SIZE template default. */
TEST(CobsCodec, DefaultTemplateParameter) {
  CobsCodec<> codec;
  EXPECT_EQ(codec.config().maxFrameSize, apex::protocols::cobs::DEFAULT_MAX_FRAME_SIZE);
}

/** @test Verify decoder starts in idle state. */
TEST(CobsCodec, InitialStateIdle) {
  CobsCodec<64> codec;
  EXPECT_EQ(codec.state().mode, apex::protocols::cobs::DecodeState::Mode::IDLE);
  EXPECT_EQ(codec.decodedLen(), 0U);
}

/** @test Verify encode buffer size calculation. */
TEST(CobsCodec, EncodeBufSizeConstexpr) {
  // 100 bytes payload: 100 + 100/254 + 2 = 100 + 0 + 2 = 102
  EXPECT_EQ(CobsCodec<100>::ENCODE_BUF_SIZE, 102U);
  // 1 byte payload: 1 + 1/254 + 2 = 1 + 0 + 2 = 3
  EXPECT_EQ(CobsCodec<1>::ENCODE_BUF_SIZE, 3U);
  // 254 bytes payload: 254 + 254/254 + 2 = 254 + 1 + 2 = 257
  EXPECT_EQ(CobsCodec<254>::ENCODE_BUF_SIZE, 257U);
}

/* ----------------------------- Encode Tests ----------------------------- */

/** @test Verify encoding a simple payload. */
TEST(CobsCodec, EncodeSimple) {
  CobsCodec<64> codec;
  const uint8_t DATA[] = {0x01, 0x02, 0x03};

  const auto RESULT = codec.encode(Span(DATA, sizeof(DATA)));

  EXPECT_EQ(RESULT.status, Status::OK);
  // COBS: code byte (0x04) + 3 data + delimiter = 5
  EXPECT_EQ(RESULT.bytesProduced, 5U);

  const uint8_t* OUT = codec.encodeBuf();
  EXPECT_EQ(OUT[0], 0x04); // code byte: 3 data bytes + 1
  EXPECT_EQ(OUT[1], 0x01);
  EXPECT_EQ(OUT[2], 0x02);
  EXPECT_EQ(OUT[3], 0x03);
  EXPECT_EQ(OUT[4], DELIMITER);
}

/** @test Verify encoding without trailing delimiter. */
TEST(CobsCodec, EncodeNoTrailingDelimiter) {
  CobsCodec<64> codec;
  const uint8_t DATA[] = {0x41};

  const auto RESULT = codec.encode(Span(DATA, 1), false);

  EXPECT_EQ(RESULT.status, Status::OK);
  EXPECT_EQ(RESULT.bytesProduced, 2U); // code + data, no delimiter
}

/** @test Verify encoding empty payload. */
TEST(CobsCodec, EncodeEmptyPayload) {
  CobsCodec<64> codec;

  const auto RESULT = codec.encode(Span());

  EXPECT_EQ(RESULT.status, Status::OK);
  EXPECT_EQ(RESULT.bytesProduced, 2U); // code byte (0x01) + delimiter
  EXPECT_EQ(codec.encodeBuf()[0], 0x01);
  EXPECT_EQ(codec.encodeBuf()[1], DELIMITER);
}

/* ----------------------------- Decode Tests ----------------------------- */

/** @test Verify decoding a simple COBS frame. */
TEST(CobsCodec, DecodeSimpleFrame) {
  CobsCodec<64> codec;
  // Encode {0x0A, 0x0B, 0x0C} -> [0x04, 0x0A, 0x0B, 0x0C, 0x00]
  const uint8_t WIRE[] = {0x04, 0x0A, 0x0B, 0x0C, DELIMITER};

  const auto RESULT = codec.feedDecode(Span(WIRE, sizeof(WIRE)));

  EXPECT_EQ(RESULT.status, Status::OK);
  EXPECT_TRUE(RESULT.frameCompleted);
  EXPECT_EQ(codec.decodedLen(), 3U);

  const uint8_t* BUF = codec.decodeBuf();
  EXPECT_EQ(BUF[0], 0x0A);
  EXPECT_EQ(BUF[1], 0x0B);
  EXPECT_EQ(BUF[2], 0x0C);
}

/** @test Verify decodedPayload returns correct span. */
TEST(CobsCodec, DecodedPayloadSpan) {
  CobsCodec<64> codec;
  // Encode {0xFF, 0xFE} -> [0x03, 0xFF, 0xFE, 0x00]
  const uint8_t WIRE[] = {0x03, 0xFF, 0xFE, DELIMITER};

  codec.feedDecode(Span(WIRE, sizeof(WIRE)));

  const auto PAYLOAD = codec.decodedPayload();
  EXPECT_EQ(PAYLOAD.size(), 2U);
  EXPECT_EQ(PAYLOAD[0], 0xFF);
  EXPECT_EQ(PAYLOAD[1], 0xFE);
}

/** @test Verify partial frame returns NEED_MORE with frameCompleted=false. */
TEST(CobsCodec, DecodePartialFrame) {
  CobsCodec<64> codec;
  // Code byte says 3 data bytes follow, but we only give 2 (no delimiter)
  const uint8_t WIRE[] = {0x04, 0x01, 0x02};

  const auto RESULT = codec.feedDecode(Span(WIRE, sizeof(WIRE)));

  EXPECT_EQ(RESULT.status, Status::NEED_MORE);
  EXPECT_FALSE(RESULT.frameCompleted);
}

/** @test Verify streaming decode across multiple feedDecode calls. */
TEST(CobsCodec, DecodeStreaming) {
  CobsCodec<64> codec;

  // First encode to get valid wire bytes
  const uint8_t ORIGINAL[] = {0x01, 0x02, 0x03};
  CobsCodec<64> enc;
  auto er = enc.encode(Span(ORIGINAL, sizeof(ORIGINAL)));
  ASSERT_EQ(er.status, Status::OK);

  // Split encoded data into two chunks
  const size_t CUT = 2;
  auto r1 = codec.feedDecode(Span(enc.encodeBuf(), CUT));
  EXPECT_FALSE(r1.frameCompleted);

  auto r2 = codec.feedDecode(Span(enc.encodeBuf() + CUT, er.bytesProduced - CUT));
  EXPECT_EQ(r2.status, Status::OK);
  EXPECT_TRUE(r2.frameCompleted);
  EXPECT_EQ(codec.decodedLen(), sizeof(ORIGINAL));
}

/** @test Verify resetDecoder clears state. */
TEST(CobsCodec, ResetDecoder) {
  CobsCodec<64> codec;
  // Feed partial data to move out of IDLE
  const uint8_t WIRE[] = {0x03, 0x01};
  codec.feedDecode(Span(WIRE, sizeof(WIRE)));

  codec.resetDecoder();
  EXPECT_EQ(codec.state().mode, apex::protocols::cobs::DecodeState::Mode::IDLE);
  EXPECT_EQ(codec.decodedLen(), 0U);
}

/* ----------------------------- Roundtrip Tests ----------------------------- */

/** @test Verify encode then decode roundtrip preserves data. */
TEST(CobsCodec, RoundtripSimple) {
  CobsCodec<128> codec;
  const uint8_t ORIGINAL[] = {0x10, 0x20, 0x30, 0x40, 0x50};

  const auto ENC = codec.encode(Span(ORIGINAL, sizeof(ORIGINAL)));
  EXPECT_EQ(ENC.status, Status::OK);

  codec.resetDecoder();
  const auto DEC = codec.feedDecode(Span(codec.encodeBuf(), ENC.bytesProduced));
  EXPECT_EQ(DEC.status, Status::OK);
  EXPECT_TRUE(DEC.frameCompleted);
  EXPECT_EQ(codec.decodedLen(), sizeof(ORIGINAL));
  EXPECT_EQ(memcmp(codec.decodeBuf(), ORIGINAL, sizeof(ORIGINAL)), 0);
}

/** @test Verify roundtrip with zero bytes in payload. */
TEST(CobsCodec, RoundtripWithZeros) {
  CobsCodec<128> codec;
  const uint8_t ORIGINAL[] = {0x00, 0x01, 0x00, 0x02, 0x00};

  const auto ENC = codec.encode(Span(ORIGINAL, sizeof(ORIGINAL)));
  EXPECT_EQ(ENC.status, Status::OK);

  codec.resetDecoder();
  const auto DEC = codec.feedDecode(Span(codec.encodeBuf(), ENC.bytesProduced));
  EXPECT_EQ(DEC.status, Status::OK);
  EXPECT_TRUE(DEC.frameCompleted);
  EXPECT_EQ(codec.decodedLen(), sizeof(ORIGINAL));
  EXPECT_EQ(memcmp(codec.decodeBuf(), ORIGINAL, sizeof(ORIGINAL)), 0);
}

/** @test Verify roundtrip at maximum frame size. */
TEST(CobsCodec, RoundtripMaxSize) {
  CobsCodec<64> codec;
  uint8_t original[64];
  for (size_t i = 0; i < 64; ++i) {
    original[i] = static_cast<uint8_t>(i & 0xFF);
  }

  const auto ENC = codec.encode(Span(original, 64));
  EXPECT_EQ(ENC.status, Status::OK);

  codec.resetDecoder();
  const auto DEC = codec.feedDecode(Span(codec.encodeBuf(), ENC.bytesProduced));
  EXPECT_EQ(DEC.status, Status::OK);
  EXPECT_TRUE(DEC.frameCompleted);
  EXPECT_EQ(codec.decodedLen(), 64U);
  EXPECT_EQ(memcmp(codec.decodeBuf(), original, 64), 0);
}

/* ----------------------------- Config Tests ----------------------------- */

/** @test Verify config is modifiable. */
TEST(CobsCodec, ConfigModifiable) {
  CobsCodec<256> codec;

  codec.config().dropUntilDelimiter = false;
  codec.config().requireTrailingDelimiter = false;

  EXPECT_FALSE(codec.config().dropUntilDelimiter);
  EXPECT_FALSE(codec.config().requireTrailingDelimiter);
}

/** @test Verify small codec template parameter. */
TEST(CobsCodec, SmallCodec) {
  CobsCodec<8> codec;
  EXPECT_EQ(codec.config().maxFrameSize, 8U);

  const uint8_t DATA[] = {0x01, 0x02, 0x03};
  const auto ENC = codec.encode(Span(DATA, sizeof(DATA)));
  EXPECT_EQ(ENC.status, Status::OK);
}
