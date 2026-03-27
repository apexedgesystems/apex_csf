/**
 * @file SLIPFraming_uTest.cpp
 * @brief Unit tests for SLIP encoding and streaming decode.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - Tests verify correctness of both encode and decode paths.
 */

#include "src/system/core/infrastructure/protocols/framing/slip/inc/SLIPFraming.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

using apex::protocols::slip::decodeChunk;
using apex::protocols::slip::DecodeConfig;
using apex::protocols::slip::DecodeState;
using apex::protocols::slip::DEFAULT_MAX_FRAME_SIZE;
using apex::protocols::slip::encode;
using apex::protocols::slip::encodePreSized;
using apex::protocols::slip::END;
using apex::protocols::slip::ESC;
using apex::protocols::slip::ESC_END;
using apex::protocols::slip::ESC_ESC;
using apex::protocols::slip::IoResult;
using apex::protocols::slip::Status;
using apex::protocols::slip::toString;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default DecodeConfig has reasonable defaults. */
TEST(DecodeConfigDefaultTest, DefaultValues) {
  const DecodeConfig CFG{};
  EXPECT_EQ(CFG.maxFrameSize, DEFAULT_MAX_FRAME_SIZE);
  EXPECT_FALSE(CFG.allowEmptyFrame);
  EXPECT_TRUE(CFG.dropUntilEnd);
  EXPECT_TRUE(CFG.requireTrailingEnd);
}

/** @test Default DecodeState is in IDLE mode with zero counters. */
TEST(DecodeStateDefaultTest, DefaultValues) {
  const DecodeState ST{};
  EXPECT_EQ(ST.mode, DecodeState::Mode::IDLE);
  EXPECT_EQ(ST.frameLen, 0u);
  EXPECT_FALSE(ST.hadData);
}

/** @test Default IoResult has OK status and zero counters. */
TEST(IoResultDefaultTest, DefaultValues) {
  const IoResult R{};
  EXPECT_EQ(R.status, Status::OK);
  EXPECT_EQ(R.bytesConsumed, 0u);
  EXPECT_EQ(R.bytesProduced, 0u);
  EXPECT_FALSE(R.frameCompleted);
  EXPECT_EQ(R.needed, 0u);
}

/* ----------------------------- Status Tests ----------------------------- */

/** @test toString returns non-null for all Status values. */
TEST(StatusTest, ToStringNonNull) {
  EXPECT_NE(toString(Status::OK), nullptr);
  EXPECT_NE(toString(Status::NEED_MORE), nullptr);
  EXPECT_NE(toString(Status::OUTPUT_FULL), nullptr);
  EXPECT_NE(toString(Status::ERROR_MISSING_DELIMITER), nullptr);
  EXPECT_NE(toString(Status::ERROR_INCOMPLETE_ESCAPE), nullptr);
  EXPECT_NE(toString(Status::ERROR_INVALID_ESCAPE), nullptr);
  EXPECT_NE(toString(Status::ERROR_DECODE_FAILED), nullptr);
  EXPECT_NE(toString(Status::ERROR_OVERSIZE), nullptr);
}

/** @test toString handles invalid status values. */
TEST(StatusTest, ToStringHandlesInvalid) {
  const auto INVALID = static_cast<Status>(255);
  const char* RESULT = toString(INVALID);
  EXPECT_NE(RESULT, nullptr);
  EXPECT_STREQ(RESULT, "UNKNOWN");
}

/* ----------------------------- Encode Tests ----------------------------- */

/** @test Encoding adds start and end delimiters. */
TEST(EncodeTest, AddsDelimiters) {
  std::vector<uint8_t> payload{0xEF, 0xBE, 0xAD, 0xDE};
  std::vector<uint8_t> encoded(16);

  auto r = encode(apex::compat::bytes_span{payload.data(), payload.size()}, encoded.data(),
                  encoded.size());

  EXPECT_EQ(r.status, Status::OK);
  EXPECT_EQ(encoded[0], END);
  EXPECT_EQ(encoded[r.bytesProduced - 1], END);
}

/** @test Encoding escapes END and ESC bytes correctly. */
TEST(EncodeTest, EscapesSpecialBytes) {
  std::vector<uint8_t> payload{END, ESC};
  std::vector<uint8_t> encoded(32);

  auto r = encode(apex::compat::bytes_span{payload.data(), payload.size()}, encoded.data(),
                  encoded.size());

  EXPECT_EQ(r.status, Status::OK);
  // Should contain: END, ESC, ESC_END, ESC, ESC_ESC, END = 6 bytes
  EXPECT_EQ(r.bytesProduced, 6u);
}

/** @test Encoding with insufficient buffer returns OUTPUT_FULL. */
TEST(EncodeTest, OutputFullOnSmallBuffer) {
  std::vector<uint8_t> payload{0x01, 0x02, 0x03, 0x04};
  std::vector<uint8_t> encoded(2); // Too small

  auto r = encode(apex::compat::bytes_span{payload.data(), payload.size()}, encoded.data(),
                  encoded.size());

  EXPECT_EQ(r.status, Status::OUTPUT_FULL);
  EXPECT_GT(r.needed, encoded.size());
}

/** @test encodePreSized works for pre-sized buffers. */
TEST(EncodePreSizedTest, BasicEncoding) {
  std::vector<uint8_t> payload{0xEF, 0xBE, 0xAD, 0xDE};
  std::vector<uint8_t> encoded(payload.size() * 2 + 2);

  auto r = encodePreSized(apex::compat::bytes_span{payload.data(), payload.size()}, encoded.data(),
                          encoded.size());

  EXPECT_EQ(r.status, Status::OK);
  EXPECT_GT(r.bytesProduced, 0u);
}

/* ----------------------------- Decode Tests ----------------------------- */

/** @test Decoding recovers original payload. */
TEST(DecodeTest, RecoversPayload) {
  std::vector<uint8_t> payload{0xEF, 0xBE, 0xAD, 0xDE};
  std::vector<uint8_t> encoded(16);

  auto r = encode(apex::compat::bytes_span{payload.data(), payload.size()}, encoded.data(),
                  encoded.size());
  encoded.resize(r.bytesProduced);

  DecodeState st{};
  DecodeConfig cfg{};
  std::vector<uint8_t> decoded(payload.size());

  auto d = decodeChunk(st, cfg, apex::compat::bytes_span{encoded.data(), encoded.size()},
                       decoded.data(), decoded.size());

  EXPECT_EQ(d.status, Status::OK);
  EXPECT_TRUE(d.frameCompleted);
  decoded.resize(d.bytesProduced);
  EXPECT_EQ(decoded, payload);
}

/** @test Decoding handles escaped bytes correctly. */
TEST(DecodeTest, HandlesEscapedBytes) {
  std::vector<uint8_t> payload{END, ESC, ESC, END};
  std::vector<uint8_t> encoded(32);

  auto r = encode(apex::compat::bytes_span{payload.data(), payload.size()}, encoded.data(),
                  encoded.size());
  encoded.resize(r.bytesProduced);

  DecodeState st{};
  DecodeConfig cfg{};
  std::vector<uint8_t> decoded(payload.size());

  auto d = decodeChunk(st, cfg, apex::compat::bytes_span{encoded.data(), encoded.size()},
                       decoded.data(), decoded.size());

  EXPECT_EQ(d.status, Status::OK);
  EXPECT_TRUE(d.frameCompleted);
  decoded.resize(d.bytesProduced);
  EXPECT_EQ(decoded, payload);
}

/** @test Decoding reassembles frame split across chunks. */
TEST(DecodeTest, ReassemblesChunks) {
  std::vector<uint8_t> payload{0xEF, 0xBE, 0xAD, 0xDE};
  std::vector<uint8_t> encoded(16);
  auto r = encode(apex::compat::bytes_span{payload.data(), payload.size()}, encoded.data(),
                  encoded.size());
  encoded.resize(r.bytesProduced);

  std::vector<uint8_t> first(encoded.begin(), encoded.begin() + 1);
  std::vector<uint8_t> second(encoded.begin() + 1, encoded.end());

  DecodeState st{};
  DecodeConfig cfg{};
  std::vector<uint8_t> decoded(payload.size());

  auto d1 = decodeChunk(st, cfg, apex::compat::bytes_span{first.data(), first.size()},
                        decoded.data(), decoded.size());

  EXPECT_EQ(d1.status, Status::OK);
  EXPECT_FALSE(d1.frameCompleted);

  auto d2 = decodeChunk(st, cfg, apex::compat::bytes_span{second.data(), second.size()},
                        decoded.data(), decoded.size());

  EXPECT_EQ(d2.status, Status::OK);
  EXPECT_TRUE(d2.frameCompleted);
  decoded.resize(d2.bytesProduced);
  EXPECT_EQ(decoded, payload);
}

/** @test Decoding handles multiple frames in stream. */
TEST(DecodeTest, MultipleFrames) {
  std::vector<uint8_t> pkt0{0xEF, 0xBE, 0xAD, 0xDE};
  std::vector<uint8_t> pkt1{0xEF, 0xCA};
  std::vector<uint8_t> enc0(32), enc1(32);

  auto r0 = encode(apex::compat::bytes_span{pkt0.data(), pkt0.size()}, enc0.data(), enc0.size());
  enc0.resize(r0.bytesProduced);

  auto r1 = encode(apex::compat::bytes_span{pkt1.data(), pkt1.size()}, enc1.data(), enc1.size());
  enc1.resize(r1.bytesProduced);

  std::vector<uint8_t> stream;
  stream.insert(stream.end(), enc0.begin(), enc0.end());
  stream.insert(stream.end(), enc1.begin(), enc1.end());

  DecodeState st{};
  DecodeConfig cfg{};
  std::vector<std::vector<uint8_t>> frames;

  std::vector<uint8_t> buf(64);
  auto d = decodeChunk(st, cfg, apex::compat::bytes_span{stream.data(), stream.size()}, buf.data(),
                       buf.size());

  EXPECT_EQ(d.status, Status::OK);
  EXPECT_TRUE(d.frameCompleted);
  buf.resize(d.bytesProduced);
  frames.push_back(buf);

  if (d.bytesConsumed < stream.size()) {
    buf.assign(64, 0);
    auto d2 = decodeChunk(
        st, cfg,
        apex::compat::bytes_span{stream.data() + d.bytesConsumed, stream.size() - d.bytesConsumed},
        buf.data(), buf.size());
    EXPECT_EQ(d2.status, Status::OK);
    EXPECT_TRUE(d2.frameCompleted);
    buf.resize(d2.bytesProduced);
    frames.push_back(buf);
  }

  ASSERT_EQ(frames.size(), 2u);
  EXPECT_EQ(frames[0], pkt0);
  EXPECT_EQ(frames[1], pkt1);
}

/* ----------------------------- Error Handling Tests ----------------------------- */

/** @test Decoder resynchronizes after junk data. */
TEST(DecodeErrorTest, SyncRecovery) {
  std::vector<uint8_t> pkt{0xEF, 0xBE, 0xAD, 0xDE};
  std::vector<uint8_t> enc(32);
  auto r = encode(apex::compat::bytes_span{pkt.data(), pkt.size()}, enc.data(), enc.size());
  enc.resize(r.bytesProduced);

  std::vector<uint8_t> stream;
  stream.push_back(0xFF);
  stream.insert(stream.end(), enc.begin(), enc.end());

  DecodeState st{};
  DecodeConfig cfg{};
  std::vector<uint8_t> decoded(pkt.size());

  auto d = decodeChunk(st, cfg, apex::compat::bytes_span{stream.data(), stream.size()},
                       decoded.data(), decoded.size());

  EXPECT_EQ(d.status, Status::OK);
  EXPECT_TRUE(d.frameCompleted);
  decoded.resize(d.bytesProduced);
  EXPECT_EQ(decoded, pkt);
}

/** @test Decoder signals ERROR_OVERSIZE for frames exceeding maxFrameSize. */
TEST(DecodeErrorTest, OversizeFrame) {
  DecodeConfig cfg{};
  cfg.maxFrameSize = 4;

  std::vector<uint8_t> payload(8, 0xAA);
  std::vector<uint8_t> enc(payload.size() * 2 + 2);
  auto r = encode(apex::compat::bytes_span{payload.data(), payload.size()}, enc.data(), enc.size());
  enc.resize(r.bytesProduced);

  DecodeState st{};
  std::vector<uint8_t> decoded(cfg.maxFrameSize);

  auto d = decodeChunk(st, cfg, apex::compat::bytes_span{enc.data(), enc.size()}, decoded.data(),
                       decoded.size());

  EXPECT_EQ(d.status, Status::ERROR_OVERSIZE);
  EXPECT_FALSE(d.frameCompleted);
}

/** @test Decoder returns NEED_MORE when chunk ends after ESC. */
TEST(DecodeErrorTest, NeedMoreEscapeAtEnd) {
  std::vector<uint8_t> payload{END};
  std::vector<uint8_t> enc(payload.size() * 2 + 2);
  auto r = encode(apex::compat::bytes_span{payload.data(), payload.size()}, enc.data(), enc.size());
  enc.resize(r.bytesProduced);

  auto it = std::find(enc.begin(), enc.end(), ESC);
  ASSERT_NE(it, enc.end());

  std::size_t prefixLen = static_cast<std::size_t>(std::distance(enc.begin(), it + 1));
  std::vector<uint8_t> first(enc.begin(), enc.begin() + prefixLen);
  std::vector<uint8_t> second(enc.begin() + prefixLen, enc.end());

  DecodeState st{};
  DecodeConfig cfg{};
  std::vector<uint8_t> buf(payload.size());

  auto d1 = decodeChunk(st, cfg, apex::compat::bytes_span{first.data(), first.size()}, buf.data(),
                        buf.size());

  EXPECT_EQ(d1.status, Status::NEED_MORE);
  EXPECT_FALSE(d1.frameCompleted);

  auto d2 = decodeChunk(st, cfg, apex::compat::bytes_span{second.data(), second.size()}, buf.data(),
                        buf.size());

  EXPECT_EQ(d2.status, Status::OK);
  EXPECT_TRUE(d2.frameCompleted);
}

/** @test Decoder returns OUTPUT_FULL and resumes correctly. */
TEST(DecodeErrorTest, OutputFullAndResume) {
  std::vector<uint8_t> payload{0x01, 0x02, 0x03, 0x04, 0x05};
  std::vector<uint8_t> enc(payload.size() * 2 + 2);
  auto r = encode(apex::compat::bytes_span{payload.data(), payload.size()}, enc.data(), enc.size());
  enc.resize(r.bytesProduced);

  DecodeState st{};
  DecodeConfig cfg{};
  std::vector<uint8_t> buf(2);

  auto d1 = decodeChunk(st, cfg, apex::compat::bytes_span{enc.data(), enc.size()}, buf.data(),
                        buf.size());

  std::vector<uint8_t> acc;
  acc.insert(acc.end(), buf.begin(), buf.begin() + d1.bytesProduced);

  EXPECT_EQ(d1.status, Status::OUTPUT_FULL);
  EXPECT_FALSE(d1.frameCompleted);

  std::vector<uint8_t> buf2(payload.size());
  auto d2 = decodeChunk(
      st, cfg,
      apex::compat::bytes_span{enc.data() + d1.bytesConsumed, enc.size() - d1.bytesConsumed},
      buf2.data(), buf2.size());

  acc.insert(acc.end(), buf2.begin(), buf2.begin() + d2.bytesProduced);

  EXPECT_EQ(d2.status, Status::OK);
  EXPECT_TRUE(d2.frameCompleted);
  EXPECT_EQ(acc, payload);
}

/* ----------------------------- Optimization Path Tests ----------------------------- */

/** @test Small payload uses branchless escape counting. */
TEST(OptimizationTest, SmallPayloadPath) {
  std::vector<uint8_t> payload(128);
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<uint8_t>(i);
  }

  std::vector<uint8_t> enc(payload.size() * 2 + 2);
  auto r = encode(apex::compat::bytes_span{payload.data(), payload.size()}, enc.data(), enc.size());
  enc.resize(r.bytesProduced);

  EXPECT_EQ(r.status, Status::OK);

  DecodeState st{};
  DecodeConfig cfg{};
  std::vector<uint8_t> decoded(payload.size());
  auto d = decodeChunk(st, cfg, apex::compat::bytes_span{enc.data(), enc.size()}, decoded.data(),
                       decoded.size());

  EXPECT_EQ(d.status, Status::OK);
  EXPECT_TRUE(d.frameCompleted);
  decoded.resize(d.bytesProduced);
  EXPECT_EQ(decoded, payload);
}

/** @test Large payload uses SIMD-optimized memchr. */
TEST(OptimizationTest, LargePayloadPath) {
  std::vector<uint8_t> payload(512);
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<uint8_t>(i & 0xFF);
  }

  std::vector<uint8_t> enc(payload.size() * 2 + 2);
  auto r = encode(apex::compat::bytes_span{payload.data(), payload.size()}, enc.data(), enc.size());
  enc.resize(r.bytesProduced);

  EXPECT_EQ(r.status, Status::OK);

  DecodeState st{};
  DecodeConfig cfg{};
  std::vector<uint8_t> decoded(payload.size());
  auto d = decodeChunk(st, cfg, apex::compat::bytes_span{enc.data(), enc.size()}, decoded.data(),
                       decoded.size());

  EXPECT_EQ(d.status, Status::OK);
  EXPECT_TRUE(d.frameCompleted);
  decoded.resize(d.bytesProduced);
  EXPECT_EQ(decoded, payload);
}

/** @test Bulk copy optimization for clean data ranges. */
TEST(OptimizationTest, BulkCopyPath) {
  std::vector<uint8_t> payload(512, 0x42);

  std::vector<uint8_t> enc(payload.size() + 4);
  auto r = encode(apex::compat::bytes_span{payload.data(), payload.size()}, enc.data(), enc.size());
  enc.resize(r.bytesProduced);

  EXPECT_EQ(r.status, Status::OK);

  DecodeState st{};
  DecodeConfig cfg{};
  std::vector<uint8_t> decoded(payload.size());
  auto d = decodeChunk(st, cfg, apex::compat::bytes_span{enc.data(), enc.size()}, decoded.data(),
                       decoded.size());

  EXPECT_EQ(d.status, Status::OK);
  EXPECT_TRUE(d.frameCompleted);
  decoded.resize(d.bytesProduced);
  EXPECT_EQ(decoded, payload);
}

/** @test Edge case at 256-byte threshold boundary. */
TEST(OptimizationTest, ThresholdBoundary) {
  std::vector<uint8_t> payload(256, 0x55);

  std::vector<uint8_t> enc(payload.size() + 4);
  auto r = encode(apex::compat::bytes_span{payload.data(), payload.size()}, enc.data(), enc.size());
  enc.resize(r.bytesProduced);

  EXPECT_EQ(r.status, Status::OK);

  DecodeState st{};
  DecodeConfig cfg{};
  std::vector<uint8_t> decoded(payload.size());
  auto d = decodeChunk(st, cfg, apex::compat::bytes_span{enc.data(), enc.size()}, decoded.data(),
                       decoded.size());

  EXPECT_EQ(d.status, Status::OK);
  EXPECT_TRUE(d.frameCompleted);
  decoded.resize(d.bytesProduced);
  EXPECT_EQ(decoded, payload);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test Repeated encode produces identical results. */
TEST(DeterminismTest, EncodeConsistent) {
  std::vector<uint8_t> payload{0xEF, 0xBE, 0xAD, 0xDE};
  std::vector<uint8_t> enc1(16), enc2(16);

  auto r1 =
      encode(apex::compat::bytes_span{payload.data(), payload.size()}, enc1.data(), enc1.size());
  auto r2 =
      encode(apex::compat::bytes_span{payload.data(), payload.size()}, enc2.data(), enc2.size());

  EXPECT_EQ(r1.status, r2.status);
  EXPECT_EQ(r1.bytesProduced, r2.bytesProduced);

  enc1.resize(r1.bytesProduced);
  enc2.resize(r2.bytesProduced);
  EXPECT_EQ(enc1, enc2);
}

/** @test DecodeState reset returns to initial state. */
TEST(DeterminismTest, DecodeStateReset) {
  DecodeState st{};
  st.mode = DecodeState::Mode::IN_FRAME;
  st.frameLen = 100;
  st.hadData = true;

  st.reset();

  EXPECT_EQ(st.mode, DecodeState::Mode::IDLE);
  EXPECT_EQ(st.frameLen, 0u);
  EXPECT_FALSE(st.hadData);
}
