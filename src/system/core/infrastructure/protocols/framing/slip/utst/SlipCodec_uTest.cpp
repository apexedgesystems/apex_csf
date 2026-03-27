/**
 * @file SlipCodec_uTest.cpp
 * @brief Unit tests for the SlipCodec templated wrapper.
 *
 * Tests the convenience wrapper around the raw SLIP encode/decode API.
 * Verifies buffer ownership, config propagation, and encode/decode roundtrips
 * at various template-parameterized sizes.
 */

#include "src/system/core/infrastructure/protocols/framing/slip/inc/SlipCodec.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

using apex::protocols::slip::END;
using apex::protocols::slip::SlipCodec;
using apex::protocols::slip::Status;
using Span = apex::compat::bytes_span;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verify default codec has correct config maxFrameSize. */
TEST(SlipCodec, DefaultConfigMaxFrameSize) {
  SlipCodec<128> codec;
  EXPECT_EQ(codec.config().maxFrameSize, 128U);
}

/** @test Verify default codec uses DEFAULT_MAX_FRAME_SIZE template default. */
TEST(SlipCodec, DefaultTemplateParameter) {
  SlipCodec<> codec;
  EXPECT_EQ(codec.config().maxFrameSize, apex::protocols::slip::DEFAULT_MAX_FRAME_SIZE);
}

/** @test Verify decoder starts in idle state. */
TEST(SlipCodec, InitialStateIdle) {
  SlipCodec<64> codec;
  EXPECT_EQ(codec.state().mode, apex::protocols::slip::DecodeState::Mode::IDLE);
  EXPECT_EQ(codec.decodedLen(), 0U);
}

/** @test Verify encode buffer size calculation. */
TEST(SlipCodec, EncodeBufSizeConstexpr) {
  EXPECT_EQ(SlipCodec<100>::ENCODE_BUF_SIZE, 202U);
  EXPECT_EQ(SlipCodec<1>::ENCODE_BUF_SIZE, 4U);
}

/* ----------------------------- Encode Tests ----------------------------- */

/** @test Verify encoding a simple payload. */
TEST(SlipCodec, EncodeSimple) {
  SlipCodec<64> codec;
  const uint8_t DATA[] = {0x01, 0x02, 0x03};

  const auto RESULT = codec.encode(Span(DATA, sizeof(DATA)));

  EXPECT_EQ(RESULT.status, Status::OK);
  EXPECT_EQ(RESULT.bytesProduced, 5U); // END + 3 + END

  const uint8_t* OUT = codec.encodeBuf();
  EXPECT_EQ(OUT[0], END);
  EXPECT_EQ(OUT[1], 0x01);
  EXPECT_EQ(OUT[2], 0x02);
  EXPECT_EQ(OUT[3], 0x03);
  EXPECT_EQ(OUT[4], END);
}

/** @test Verify encoding without leading delimiter. */
TEST(SlipCodec, EncodeNoLeadingEnd) {
  SlipCodec<64> codec;
  const uint8_t DATA[] = {0x41};

  const auto RESULT = codec.encode(Span(DATA, 1), false, true);

  EXPECT_EQ(RESULT.status, Status::OK);
  EXPECT_EQ(RESULT.bytesProduced, 2U); // data + END
}

/** @test Verify encoding empty payload produces delimiters only. */
TEST(SlipCodec, EncodeEmptyPayload) {
  SlipCodec<64> codec;

  const auto RESULT = codec.encode(Span());

  EXPECT_EQ(RESULT.status, Status::OK);
  EXPECT_EQ(RESULT.bytesProduced, 2U); // END + END
}

/* ----------------------------- Decode Tests ----------------------------- */

/** @test Verify decoding a simple SLIP frame. */
TEST(SlipCodec, DecodeSimpleFrame) {
  SlipCodec<64> codec;
  const uint8_t WIRE[] = {END, 0x0A, 0x0B, 0x0C, END};

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
TEST(SlipCodec, DecodedPayloadSpan) {
  SlipCodec<64> codec;
  const uint8_t WIRE[] = {END, 0xFF, 0xFE, END};

  codec.feedDecode(Span(WIRE, sizeof(WIRE)));

  const auto PAYLOAD = codec.decodedPayload();
  EXPECT_EQ(PAYLOAD.size(), 2U);
  EXPECT_EQ(PAYLOAD[0], 0xFF);
  EXPECT_EQ(PAYLOAD[1], 0xFE);
}

/** @test Verify partial frame returns OK with frameCompleted=false. */
TEST(SlipCodec, DecodePartialFrame) {
  SlipCodec<64> codec;
  const uint8_t WIRE[] = {END, 0x01, 0x02}; // No trailing END

  const auto RESULT = codec.feedDecode(Span(WIRE, sizeof(WIRE)));

  EXPECT_EQ(RESULT.status, Status::OK);
  EXPECT_FALSE(RESULT.frameCompleted);
}

/** @test Verify streaming decode across multiple feedDecode calls. */
TEST(SlipCodec, DecodeStreaming) {
  SlipCodec<64> codec;
  const uint8_t CHUNK1[] = {END, 0x01, 0x02};
  const uint8_t CHUNK2[] = {0x03, END};

  auto r1 = codec.feedDecode(Span(CHUNK1, sizeof(CHUNK1)));
  EXPECT_EQ(r1.status, Status::OK);
  EXPECT_FALSE(r1.frameCompleted);

  auto r2 = codec.feedDecode(Span(CHUNK2, sizeof(CHUNK2)));
  EXPECT_EQ(r2.status, Status::OK);
  EXPECT_TRUE(r2.frameCompleted);
  EXPECT_EQ(codec.decodedLen(), 3U);
}

/** @test Verify resetDecoder clears state. */
TEST(SlipCodec, ResetDecoder) {
  SlipCodec<64> codec;
  const uint8_t WIRE[] = {END, 0x01, 0x02};

  codec.feedDecode(Span(WIRE, sizeof(WIRE)));
  EXPECT_NE(codec.state().mode, apex::protocols::slip::DecodeState::Mode::IDLE);

  codec.resetDecoder();
  EXPECT_EQ(codec.state().mode, apex::protocols::slip::DecodeState::Mode::IDLE);
  EXPECT_EQ(codec.decodedLen(), 0U);
}

/* ----------------------------- Roundtrip Tests ----------------------------- */

/** @test Verify encode then decode roundtrip preserves data. */
TEST(SlipCodec, RoundtripSimple) {
  SlipCodec<128> codec;
  const uint8_t ORIGINAL[] = {0x10, 0x20, 0x30, 0x40, 0x50};

  // Encode
  const auto ENC = codec.encode(Span(ORIGINAL, sizeof(ORIGINAL)));
  EXPECT_EQ(ENC.status, Status::OK);

  // Decode the encoded output
  codec.resetDecoder();
  const auto DEC = codec.feedDecode(Span(codec.encodeBuf(), ENC.bytesProduced));
  EXPECT_EQ(DEC.status, Status::OK);
  EXPECT_TRUE(DEC.frameCompleted);
  EXPECT_EQ(codec.decodedLen(), sizeof(ORIGINAL));
  EXPECT_EQ(memcmp(codec.decodeBuf(), ORIGINAL, sizeof(ORIGINAL)), 0);
}

/** @test Verify roundtrip with escape characters in payload. */
TEST(SlipCodec, RoundtripWithEscapes) {
  SlipCodec<128> codec;
  const uint8_t ORIGINAL[] = {END, 0xDB, 0x01, END, 0xDB}; // Contains END and ESC bytes

  const auto ENC = codec.encode(Span(ORIGINAL, sizeof(ORIGINAL)));
  EXPECT_EQ(ENC.status, Status::OK);
  // 5 bytes + 4 escapes + 2 delimiters = 11
  EXPECT_EQ(ENC.bytesProduced, 11U);

  codec.resetDecoder();
  const auto DEC = codec.feedDecode(Span(codec.encodeBuf(), ENC.bytesProduced));
  EXPECT_EQ(DEC.status, Status::OK);
  EXPECT_TRUE(DEC.frameCompleted);
  EXPECT_EQ(codec.decodedLen(), sizeof(ORIGINAL));
  EXPECT_EQ(memcmp(codec.decodeBuf(), ORIGINAL, sizeof(ORIGINAL)), 0);
}

/** @test Verify roundtrip at maximum frame size. */
TEST(SlipCodec, RoundtripMaxSize) {
  SlipCodec<64> codec;
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
TEST(SlipCodec, ConfigModifiable) {
  SlipCodec<256> codec;

  codec.config().allowEmptyFrame = true;
  codec.config().dropUntilEnd = false;

  EXPECT_TRUE(codec.config().allowEmptyFrame);
  EXPECT_FALSE(codec.config().dropUntilEnd);
}

/** @test Verify small codec template parameter. */
TEST(SlipCodec, SmallCodec) {
  SlipCodec<8> codec;
  EXPECT_EQ(codec.config().maxFrameSize, 8U);
  EXPECT_EQ(SlipCodec<8>::ENCODE_BUF_SIZE, 18U);

  const uint8_t DATA[] = {0x01, 0x02, 0x03};
  const auto ENC = codec.encode(Span(DATA, sizeof(DATA)));
  EXPECT_EQ(ENC.status, Status::OK);
}
