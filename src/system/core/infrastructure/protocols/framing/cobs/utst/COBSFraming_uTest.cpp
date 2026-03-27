/**
 * @file COBSFraming_uTest.cpp
 * @brief Unit tests for COBS encoding and streaming decode.
 *
 * Coverage:
 *  - Trailing delimiter handling
 *  - Correct handling of zero bytes in payloads
 *  - Frame reassembly from partial chunks
 *  - Multiple frames in one stream
 *  - Handling malformed data and resynchronization
 *  - Oversize frame guard and draining
 *  - NEED_MORE at chunk boundaries
 *  - OUTPUT_FULL backpressure and resume
 *  - Empty payload edge case
 *  - Leading delimiter synchronization
 *  - Hybrid optimization paths (small and large payloads)
 *  - Bulk decode optimization with run thresholds
 *  - Code byte boundary conditions (254-byte runs)
 */

#include "src/system/core/infrastructure/protocols/framing/cobs/inc/COBSFraming.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <vector>

using apex::protocols::cobs::decodeChunk;
using apex::protocols::cobs::DecodeConfig;
using apex::protocols::cobs::DecodeState;
using apex::protocols::cobs::DELIMITER;
using apex::protocols::cobs::encode;
using apex::protocols::cobs::IoResult;
using apex::protocols::cobs::Status;
using apex::protocols::cobs::toString;

/* ----------------------------- Default Construction ----------------------------- */

/**
 * @test Verifies that encoding appends a trailing delimiter
 *       and decoding recovers the original payload.
 */
TEST(COBSFramingTest, TrailingDelimiterAndRoundtrip) {
  std::vector<uint8_t> payload{0x11, 0x22, 0x33};
  std::vector<uint8_t> encoded(16);

  auto r = encode(apex::compat::bytes_span{payload.data(), payload.size()}, encoded.data(),
                  encoded.size());
  encoded.resize(r.bytesProduced);

  EXPECT_EQ(r.status, Status::OK);
  ASSERT_FALSE(encoded.empty());
  EXPECT_EQ(encoded.back(), DELIMITER);

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

/* ----------------------------- Enum Tests ----------------------------- */

/**
 * @test toString function returns stable human-readable labels for all Status values.
 */
TEST(COBSFramingTest, StatusToString) {
  EXPECT_STREQ(toString(Status::OK), "OK");
  EXPECT_STREQ(toString(Status::NEED_MORE), "NEED_MORE");
  EXPECT_STREQ(toString(Status::OUTPUT_FULL), "OUTPUT_FULL");
  EXPECT_STREQ(toString(Status::ERROR_MISSING_DELIMITER), "ERROR_MISSING_DELIMITER");
  EXPECT_STREQ(toString(Status::ERROR_DECODE), "ERROR_DECODE");
  EXPECT_STREQ(toString(Status::ERROR_OVERSIZE), "ERROR_OVERSIZE");
}

/* ----------------------------- Encode Tests ----------------------------- */

/**
 * @test Ensures payloads containing zero bytes are encoded and decoded correctly.
 */
TEST(COBSFramingTest, EncodeDecodeWithZerosInPayload) {
  std::vector<uint8_t> payload{0x11, 0x00, 0x22, 0x33};
  std::vector<uint8_t> encoded(32);

  auto r = encode(apex::compat::bytes_span{payload.data(), payload.size()}, encoded.data(),
                  encoded.size());
  encoded.resize(r.bytesProduced);
  EXPECT_EQ(r.status, Status::OK);

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

/**
 * @test Empty payload encodes to [0x01, 0x00] and decodes to empty vector.
 */
TEST(COBSFramingTest, EmptyPayload) {
  std::vector<uint8_t> payload;
  std::vector<uint8_t> enc(8);

  auto r = encode(apex::compat::bytes_span{payload.data(), payload.size()}, enc.data(), enc.size());
  enc.resize(r.bytesProduced);

  EXPECT_EQ(r.status, Status::OK);
  ASSERT_GE(enc.size(), 2u);
  EXPECT_EQ(enc[0], 0x01);
  EXPECT_EQ(enc.back(), DELIMITER);

  DecodeState st{};
  DecodeConfig cfg{};
  std::vector<uint8_t> out(1);
  auto d = decodeChunk(st, cfg, apex::compat::bytes_span{enc.data(), enc.size()}, out.data(),
                       out.size());

  EXPECT_EQ(d.status, Status::OK);
  EXPECT_TRUE(d.frameCompleted);
  EXPECT_EQ(d.bytesProduced, 0u);
}

/* ----------------------------- Decode Tests ----------------------------- */

/**
 * @test Verifies that a frame split across multiple chunks
 *       is reassembled into the original payload.
 */
TEST(COBSFramingTest, FrameSplitAcrossChunks) {
  std::vector<uint8_t> payload{0x77, 0x88, 0x00, 0x99};
  std::vector<uint8_t> encoded(32);
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

  EXPECT_TRUE(d1.status == Status::OK || d1.status == Status::NEED_MORE);
  EXPECT_FALSE(d1.frameCompleted);

  auto d2 = decodeChunk(st, cfg, apex::compat::bytes_span{second.data(), second.size()},
                        decoded.data(), decoded.size());

  EXPECT_EQ(d2.status, Status::OK);
  EXPECT_TRUE(d2.frameCompleted);
  decoded.resize(d2.bytesProduced);
  EXPECT_EQ(decoded, payload);
}

/**
 * @test Ensures that two distinct frames in one stream
 *       are decoded as separate payloads.
 */
TEST(COBSFramingTest, MultipleFramesInStream) {
  std::vector<uint8_t> pkt0{0x11, 0x22};
  std::vector<uint8_t> pkt1{0x33, 0x00, 0x44};
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

/**
 * @test Decoder ignores leading delimiters and syncs to next frame.
 */
TEST(COBSFramingTest, LeadingDelimitersSync) {
  std::vector<uint8_t> payload{0xAA, 0x00, 0xBB};
  std::vector<uint8_t> enc(32);
  auto r = encode(apex::compat::bytes_span{payload.data(), payload.size()}, enc.data(), enc.size());
  enc.resize(r.bytesProduced);

  std::vector<uint8_t> stream;
  stream.push_back(DELIMITER);
  stream.push_back(DELIMITER);
  stream.insert(stream.end(), enc.begin(), enc.end());

  DecodeState st{};
  DecodeConfig cfg{};
  std::vector<uint8_t> out(64);

  auto d = decodeChunk(st, cfg, apex::compat::bytes_span{stream.data(), stream.size()}, out.data(),
                       out.size());

  EXPECT_EQ(d.status, Status::OK);
  EXPECT_TRUE(d.frameCompleted);
  std::vector<uint8_t> got(out.begin(), out.begin() + d.bytesProduced);
  EXPECT_EQ(got, payload);
}

/* ----------------------------- Error Handling Tests ----------------------------- */

/**
 * @test Decoder resynchronizes after malformed data by skipping delimiters in IDLE
 *       and proceeds to the next valid frame.
 */
TEST(COBSFramingTest, HandleBadEncodingAndResync) {
  std::vector<uint8_t> good{0xAA, 0xBB};
  std::vector<uint8_t> encGood(32);
  auto rg =
      encode(apex::compat::bytes_span{good.data(), good.size()}, encGood.data(), encGood.size());
  encGood.resize(rg.bytesProduced);

  std::vector<uint8_t> junk{DELIMITER, DELIMITER};

  std::vector<uint8_t> good2{0xCC};
  std::vector<uint8_t> encGood2(16);
  auto rg2 = encode(apex::compat::bytes_span{good2.data(), good2.size()}, encGood2.data(),
                    encGood2.size());
  encGood2.resize(rg2.bytesProduced);

  std::vector<uint8_t> stream;
  stream.insert(stream.end(), encGood.begin(), encGood.end());
  stream.insert(stream.end(), junk.begin(), junk.end());
  stream.insert(stream.end(), encGood2.begin(), encGood2.end());

  DecodeState st{};
  DecodeConfig cfg{};
  std::vector<uint8_t> buf(64);

  auto d1 = decodeChunk(st, cfg, apex::compat::bytes_span{stream.data(), stream.size()}, buf.data(),
                        buf.size());
  EXPECT_EQ(d1.status, Status::OK);
  EXPECT_TRUE(d1.frameCompleted);
  std::vector<uint8_t> f0(buf.begin(), buf.begin() + d1.bytesProduced);
  EXPECT_EQ(f0, good);

  auto d2 = decodeChunk(
      st, cfg,
      apex::compat::bytes_span{stream.data() + d1.bytesConsumed, stream.size() - d1.bytesConsumed},
      buf.data(), buf.size());
  EXPECT_EQ(d2.status, Status::OK);
  EXPECT_TRUE(d2.frameCompleted);
  std::vector<uint8_t> f1(buf.begin(), buf.begin() + d2.bytesProduced);
  EXPECT_EQ(f1, good2);
}

/**
 * @test Oversize guard: payload exceeding maxFrameSize triggers ERROR_OVERSIZE and
 *       is dropped after draining to the delimiter.
 */
TEST(COBSFramingTest, OversizeFrame) {
  DecodeConfig cfg{};
  cfg.maxFrameSize = 4;

  std::vector<uint8_t> payload(10, 0xAB);
  std::vector<uint8_t> enc(64);
  auto r = encode(apex::compat::bytes_span{payload.data(), payload.size()}, enc.data(), enc.size());
  enc.resize(r.bytesProduced);

  DecodeState st{};
  std::vector<uint8_t> out(64);

  auto d1 = decodeChunk(st, cfg, apex::compat::bytes_span{enc.data(), enc.size()}, out.data(),
                        out.size());
  EXPECT_EQ(d1.status, Status::ERROR_OVERSIZE);
  EXPECT_FALSE(d1.frameCompleted);

  std::vector<uint8_t> scratch(64);
  auto d2 = decodeChunk(
      st, cfg,
      apex::compat::bytes_span{enc.data() + d1.bytesConsumed, enc.size() - d1.bytesConsumed},
      scratch.data(), scratch.size());

  EXPECT_EQ(d2.status, Status::OK);
  EXPECT_FALSE(d2.frameCompleted);
}

/**
 * @test NEED_MORE occurs when a run is cut mid-stream across chunks.
 */
TEST(COBSFramingTest, NeedMoreMidRun) {
  std::vector<uint8_t> payload{0xDE, 0xAD, 0xBE, 0xEF};
  std::vector<uint8_t> enc(32);
  auto r = encode(apex::compat::bytes_span{payload.data(), payload.size()}, enc.data(), enc.size());
  enc.resize(r.bytesProduced);

  ASSERT_GE(enc.size(), 3u);
  std::size_t cut = 2;
  std::vector<uint8_t> first(enc.begin(), enc.begin() + cut);
  std::vector<uint8_t> second(enc.begin() + cut, enc.end());

  DecodeState st{};
  DecodeConfig cfg{};
  std::vector<uint8_t> buf(payload.size());

  auto d1 = decodeChunk(st, cfg, apex::compat::bytes_span{first.data(), first.size()}, buf.data(),
                        buf.size());

  EXPECT_TRUE(d1.status == Status::NEED_MORE || d1.status == Status::OK);
  EXPECT_FALSE(d1.frameCompleted);

  auto d2 = decodeChunk(st, cfg, apex::compat::bytes_span{second.data(), second.size()}, buf.data(),
                        buf.size());

  EXPECT_EQ(d2.status, Status::OK);
  EXPECT_TRUE(d2.frameCompleted);
}

/**
 * @test OUTPUT_FULL backpressure: partial decode, then resume with a larger buffer.
 */
TEST(COBSFramingTest, OutputFullAndResume) {
  std::vector<uint8_t> payload{0x01, 0x02, 0x00, 0x03, 0x04};
  std::vector<uint8_t> enc(64);
  auto r = encode(apex::compat::bytes_span{payload.data(), payload.size()}, enc.data(), enc.size());
  enc.resize(r.bytesProduced);

  DecodeState st{};
  DecodeConfig cfg{};

  std::vector<uint8_t> small(2);
  auto d1 = decodeChunk(st, cfg, apex::compat::bytes_span{enc.data(), enc.size()}, small.data(),
                        small.size());

  std::vector<uint8_t> acc;
  acc.insert(acc.end(), small.begin(), small.begin() + d1.bytesProduced);

  EXPECT_EQ(d1.status, Status::OUTPUT_FULL);
  EXPECT_FALSE(d1.frameCompleted);

  std::vector<uint8_t> large(payload.size());
  auto d2 = decodeChunk(
      st, cfg,
      apex::compat::bytes_span{enc.data() + d1.bytesConsumed, enc.size() - d1.bytesConsumed},
      large.data(), large.size());

  acc.insert(acc.end(), large.begin(), large.begin() + d2.bytesProduced);

  EXPECT_EQ(d2.status, Status::OK);
  EXPECT_TRUE(d2.frameCompleted);
  EXPECT_EQ(acc, payload);
}

/* ----------------------------- Optimization Path Tests ----------------------------- */

/**
 * @test Verifies small payload optimization path (under 256 bytes).
 *       Uses simple scan for deterministic timing.
 */
TEST(COBSFramingTest, SmallPayloadOptimization) {
  std::vector<uint8_t> payload(128);
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = (i % 16 == 0) ? 0x00 : static_cast<uint8_t>(i);
  }

  std::vector<uint8_t> enc(payload.size() + 16);

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

/**
 * @test Verifies large payload optimization path (over 256 bytes).
 *       Uses SIMD-optimized memchr for zero detection.
 */
TEST(COBSFramingTest, LargePayloadOptimization) {
  std::vector<uint8_t> payload(512);
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = (i % 32 == 0) ? 0x00 : static_cast<uint8_t>(i & 0xFF);
  }

  std::vector<uint8_t> enc(payload.size() + 64);

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

/**
 * @test Verifies bulk decode optimization for runs of 8 or more bytes.
 *       Uses memcpy for cache-line efficient copying.
 */
TEST(COBSFramingTest, BulkDecodeOptimization) {
  std::vector<uint8_t> payload(128, 0x42);

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

/* ----------------------------- Boundary Tests ----------------------------- */

/**
 * @test Verifies 254-byte run boundary (code byte 0xFF).
 *       Tests maximum COBS run length handling.
 */
TEST(COBSFramingTest, MaxRunLength) {
  std::vector<uint8_t> payload(254, 0x7F);

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

/**
 * @test Verifies edge case at 256-byte threshold boundary.
 */
TEST(COBSFramingTest, ThresholdBoundary) {
  std::vector<uint8_t> payload(256);
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = (i % 64 == 0) ? 0x00 : static_cast<uint8_t>(i & 0xFF);
  }

  std::vector<uint8_t> enc(payload.size() + 16);
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
