/**
 * @file CcsdsSppProcessor_uTest.cpp
 * @brief Unit tests for streaming CCSDS SPP Processor (PD+7 extractor).
 */

#include "src/system/core/infrastructure/protocols/ccsds/spp/inc/CcsdsSppCommonDefs.hpp"
#include "src/system/core/infrastructure/protocols/ccsds/spp/inc/CcsdsSppMessagePacker.hpp"
#include "src/system/core/infrastructure/protocols/ccsds/spp/inc/CcsdsSppProcessor.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace protocols::ccsds::spp;

/* ----------------------------- Test Helpers ----------------------------- */

/**
 * @brief Helper to build a minimal CCSDS SPP packet.
 *
 * Constructs a packet as:
 *   [ Primary Header (6B) ] [ Payload (N bytes) ]
 *
 * Notes:
 *  - No secondary header is included.
 *  - The Packet Data Length (PD) field is automatically computed as
 *    (payload.size() - 1) per CCSDS SPP spec.
 *  - Convenience only for unit tests; not a general-purpose builder.
 *
 * @param version    CCSDS version (0-7).
 * @param type       Packet type (0=TM, 1=TC).
 * @param apid       APID (0-2047).
 * @param seqFlags   Sequence flags (0-3).
 * @param seqCount   Sequence count (0-16383).
 * @param payload    User data bytes.
 * @return Serialized packet (header + payload).
 */
static std::vector<std::uint8_t> makePacket(std::uint8_t version, bool type, std::uint16_t apid,
                                            std::uint8_t seqFlags, std::uint16_t seqCount,
                                            const std::vector<std::uint8_t>& payload) {
  const std::uint16_t PD = static_cast<std::uint16_t>(payload.size() - 1u);
  auto phOpt =
      SppPrimaryHeader::build(version, type, /*secHdr*/ false, apid, seqFlags, seqCount, PD);
  EXPECT_TRUE(phOpt.has_value());
  if (!phOpt)
    return {};

  std::vector<std::uint8_t> pkt;
  pkt.resize(SPP_HDR_SIZE_BYTES + payload.size());
  phOpt->writeTo(pkt.data());
  if (!payload.empty()) {
    std::memcpy(pkt.data() + SPP_HDR_SIZE_BYTES, payload.data(), payload.size());
  }
  return pkt;
}

/// Capture context for RT-safe callback testing.
struct CaptureCtx {
  std::vector<std::vector<std::uint8_t>>* captured;
};

/// RT-safe packet callback for tests (captures packets into vector).
static void captureCallback(void* ctx, apex::compat::bytes_span span) noexcept {
  auto* c = static_cast<CaptureCtx*>(ctx);
  c->captured->emplace_back(span.data(), span.data() + span.size());
}

/* ----------------------------- Processor Tests ----------------------------- */

/**
 * @test Single packet extracted via zero-copy callback.
 */
TEST(SppProcessorTest, SinglePacketExtraction) {
  std::vector<uint8_t> payload{0x01, 0x02, 0x03};
  auto packet = makePacket(/*ver*/ 1, /*type*/ true, /*APID*/ 0x123,
                           /*seqFlags*/ 2, /*seqCount*/ 55, payload);
  ASSERT_EQ(packet.size(), SPP_HDR_SIZE_BYTES + payload.size());

  ProcessorDefault proc;
  std::vector<std::vector<std::uint8_t>> captured;
  CaptureCtx ctx{&captured};
  proc.setPacketCallback(captureCallback, &ctx);

  auto r = proc.process(apex::compat::bytes_span{packet.data(), packet.size()});
  EXPECT_EQ(r.status, Status::OK);
  EXPECT_EQ(r.packetsExtracted, 1u);
  EXPECT_EQ(captured.size(), 1u);
  EXPECT_EQ(captured[0], packet);
  EXPECT_EQ(proc.bufferedSize(), 0u);
}

/**
 * @test Partial then remainder -> NEED_MORE then extract.
 */
TEST(SppProcessorTest, PartialPacketExtraction) {
  std::vector<uint8_t> payload{0x0A, 0x0B, 0x0C};
  auto packet = makePacket(1, false, 0x200, 1, 20, payload);
  ASSERT_EQ(packet.size(), SPP_HDR_SIZE_BYTES + payload.size());

  ProcessorDefault proc;
  std::vector<std::vector<std::uint8_t>> captured;
  CaptureCtx ctx{&captured};
  proc.setPacketCallback(captureCallback, &ctx);

  const std::size_t SPLIT = 5; // fewer than full 9
  auto r1 = proc.process(apex::compat::bytes_span{packet.data(), SPLIT});
  EXPECT_EQ(r1.status, Status::NEED_MORE);
  EXPECT_EQ(r1.packetsExtracted, 0u);
  EXPECT_EQ(captured.size(), 0u);

  auto r2 = proc.process(apex::compat::bytes_span{packet.data() + SPLIT, packet.size() - SPLIT});
  EXPECT_EQ(r2.status, Status::OK);
  EXPECT_EQ(r2.packetsExtracted, 1u);
  ASSERT_EQ(captured.size(), 1u);
  EXPECT_EQ(captured[0], packet);
  EXPECT_EQ(proc.bufferedSize(), 0u);
}

/**
 * @test Oversize PD -> WARNING_DESYNC_DROPPED resync, then valid packet extracts.
 */
TEST(SppProcessorTest, ResynchronizationDropsThenExtracts) {
  // Build an "invalid" header by setting PD so totalLength > MAX_SPP_PACKET_LENGTH.
  // PD sits at bytes 4..5; we'll synthesize a 6-byte header array directly.
  std::vector<std::uint8_t> badHeader(6, 0x00);
  // Minimal fields: version=1 (bits 7:5), rest zeros.
  badHeader[0] = static_cast<std::uint8_t>((1u & SPP_VERSION_MASK) << SPP_VERSION_SHIFT);
  // PD = MAX_SPP_PACKET_LENGTH (this ensures 6 + (PD+1) > MAX_SPP_PACKET_LENGTH)
  const std::uint16_t PD_TOO_BIG = static_cast<std::uint16_t>(MAX_SPP_PACKET_LENGTH);
  badHeader[4] = static_cast<std::uint8_t>((PD_TOO_BIG >> 8) & 0xFF);
  badHeader[5] = static_cast<std::uint8_t>(PD_TOO_BIG & 0xFF);

  // Follow immediately with a valid packet so resync can find it.
  std::vector<uint8_t> payload{0xAA, 0xBB, 0xCC};
  auto goodPacket = makePacket(1, true, 0x010, 0, 1, payload);

  std::vector<uint8_t> stream;
  stream.insert(stream.end(), badHeader.begin(), badHeader.end());
  stream.insert(stream.end(), goodPacket.begin(), goodPacket.end());

  ProcessorDefault proc;
  // default cfg.dropUntilValidHeader = true
  std::vector<std::vector<std::uint8_t>> captured;
  CaptureCtx ctx{&captured};
  proc.setPacketCallback(captureCallback, &ctx);

  auto r = proc.process(apex::compat::bytes_span{stream.data(), stream.size()});
  // We expect at least one resync drop and one extracted packet.
  EXPECT_GE(r.resyncDrops, 1u);
  EXPECT_EQ(r.packetsExtracted, 1u);
  EXPECT_EQ(r.status, Status::OK); // packets extracted takes precedence in final status
  ASSERT_EQ(captured.size(), 1u);
  EXPECT_EQ(captured[0], goodPacket);
  EXPECT_EQ(proc.bufferedSize(), 0u);
}

/**
 * @test Oversize PD with dropUntilValidHeader=false -> ERROR_LENGTH_OVER_MAX (no drop).
 */
TEST(SppProcessorTest, OversizeNoDropReportsError) {
  std::vector<std::uint8_t> badHeader(6, 0x00);
  badHeader[0] = static_cast<std::uint8_t>((1u & SPP_VERSION_MASK) << SPP_VERSION_SHIFT);
  const std::uint16_t PD_TOO_BIG = static_cast<std::uint16_t>(MAX_SPP_PACKET_LENGTH);
  badHeader[4] = static_cast<std::uint8_t>((PD_TOO_BIG >> 8) & 0xFF);
  badHeader[5] = static_cast<std::uint8_t>(PD_TOO_BIG & 0xFF);

  ProcessorDefault proc;
  ProcessorConfig cfg = proc.config();
  cfg.dropUntilValidHeader = false;
  proc.setConfig(cfg);

  std::vector<std::vector<std::uint8_t>> captured;
  CaptureCtx ctx{&captured};
  proc.setPacketCallback(captureCallback, &ctx);

  auto r = proc.process(apex::compat::bytes_span{badHeader.data(), badHeader.size()});
  EXPECT_EQ(r.status, Status::ERROR_LENGTH_OVER_MAX);
  EXPECT_EQ(r.packetsExtracted, 0u);
  EXPECT_EQ(r.resyncDrops, 0u);
  EXPECT_EQ(captured.size(), 0u);
  // No drop occurred; the bytes remain buffered.
  EXPECT_EQ(proc.bufferedSize(), badHeader.size());
}

/**
 * @test Reset clears buffered bytes; callback vector is local so we just ensure
 *       buffer is empty after a packet and reset.
 */
TEST(SppProcessorTest, ResetClearsBuffer) {
  std::vector<uint8_t> payload{0x05, 0x06, 0x07};
  auto packet = makePacket(1, false, 0x123, 1, 30, payload);

  ProcessorDefault proc;
  std::vector<std::vector<std::uint8_t>> captured;
  CaptureCtx ctx{&captured};
  proc.setPacketCallback(captureCallback, &ctx);

  auto r = proc.process(apex::compat::bytes_span{packet.data(), packet.size()});
  EXPECT_EQ(r.packetsExtracted, 1u);
  EXPECT_EQ(proc.bufferedSize(), 0u);

  proc.reset();
  EXPECT_EQ(proc.bufferedSize(), 0u);
}

/**
 * @test Two back-to-back packets are both extracted in a single process() call.
 */
TEST(SppProcessorTest, MultiplePacketsExtraction) {
  std::vector<uint8_t> payload1{0x10, 0x20, 0x30};
  auto packet1 = makePacket(1, true, 0x111, 1, 10, payload1);

  std::vector<uint8_t> payload2{0x40, 0x50, 0x60, 0x70};
  auto packet2 = makePacket(1, false, 0x222, 2, 20, payload2);

  std::vector<uint8_t> stream;
  stream.insert(stream.end(), packet1.begin(), packet1.end());
  stream.insert(stream.end(), packet2.begin(), packet2.end());

  ProcessorDefault proc;
  std::vector<std::vector<std::uint8_t>> captured;
  CaptureCtx ctx{&captured};
  proc.setPacketCallback(captureCallback, &ctx);

  auto r = proc.process(apex::compat::bytes_span{stream.data(), stream.size()});
  EXPECT_EQ(r.status, Status::OK);
  EXPECT_EQ(r.packetsExtracted, 2u);
  ASSERT_EQ(captured.size(), 2u);
  EXPECT_EQ(captured[0], packet1);
  EXPECT_EQ(captured[1], packet2);
  EXPECT_EQ(proc.bufferedSize(), 0u);
}

/**
 * @test Feeding bytes one at a time forces the processor to compact its buffer
 *       (covering the memmove path). A complete packet should still be extracted.
 */
TEST(SppProcessorTest, BufferCompactionStress) {
  std::vector<uint8_t> payload{0xAA};
  auto packet = makePacket(0, false, 0x001, 0, 1, payload);

  ProcessorDefault proc;
  std::vector<std::vector<std::uint8_t>> captured;
  CaptureCtx ctx{&captured};
  proc.setPacketCallback(captureCallback, &ctx);

  // Feed one byte at a time. Compaction should trigger internally
  // once head_/tail_ drift far enough, but packet extraction must remain correct.
  for (size_t i = 0; i < packet.size(); i++) {
    auto r = proc.process(apex::compat::bytes_span{&packet[i], 1});
    if (i + 1 == packet.size()) {
      EXPECT_EQ(r.status, Status::OK);
      EXPECT_EQ(r.packetsExtracted, 1u);
    }
  }

  ASSERT_EQ(captured.size(), 1u);
  EXPECT_EQ(captured[0], packet);
  EXPECT_EQ(proc.bufferedSize(), 0u);
}

/**
 * @test Empty input span should produce no packets and leave the processor
 *       state untouched. Useful to validate safety in no-data RT calls.
 */
TEST(SppProcessorTest, EmptyInputNoEffect) {
  ProcessorDefault proc;
  std::vector<std::vector<std::uint8_t>> captured;
  CaptureCtx ctx{&captured};
  proc.setPacketCallback(captureCallback, &ctx);

  auto r = proc.process(apex::compat::bytes_span{});
  EXPECT_EQ(r.status, Status::OK);
  EXPECT_EQ(r.packetsExtracted, 0u);
  EXPECT_EQ(captured.size(), 0u);
  EXPECT_EQ(proc.bufferedSize(), 0u);
}

/**
 * @test Two valid packets arriving in a single stream but split across chunks.
 *       Verifies that one packet is extracted after the first chunk and the
 *       second packet is extracted after the remainder arrives.
 */
TEST(SppProcessorTest, MultiplePacketsSplitAcrossChunks) {
  std::vector<uint8_t> p1Payload{0x11, 0x22};
  auto packet1 = makePacket(0, false, 0x010, 0, 1, p1Payload);

  std::vector<uint8_t> p2Payload{0x33, 0x44, 0x55};
  auto packet2 = makePacket(0, false, 0x011, 0, 2, p2Payload);

  // Concatenate packets into a single stream, simulating a fragmented feed.
  std::vector<uint8_t> stream;
  stream.insert(stream.end(), packet1.begin(), packet1.end());
  stream.insert(stream.end(), packet2.begin(), packet2.end());

  ProcessorDefault proc;
  std::vector<std::vector<std::uint8_t>> captured;
  CaptureCtx ctx{&captured};
  proc.setPacketCallback(captureCallback, &ctx);

  // Feed stream in two parts: first packet + partial of second
  auto r1 = proc.process(apex::compat::bytes_span{stream.data(), packet1.size() + 2});
  EXPECT_EQ(r1.packetsExtracted, 1u);

  // Feed the remainder of the second packet
  auto r2 = proc.process(apex::compat::bytes_span{stream.data() + packet1.size() + 2,
                                                  stream.size() - (packet1.size() + 2)});
  EXPECT_EQ(r2.packetsExtracted, 1u);

  ASSERT_EQ(captured.size(), 2u);
  EXPECT_EQ(captured[0], packet1);
  EXPECT_EQ(captured[1], packet2);
}

/**
 * @test Header-only first chunk (exact 6 bytes) -> NEED_MORE, then extraction after tail arrives.
 * Layout: first call provides [Primary(6)] only; second call provides payload bytes.
 */
TEST(SppProcessorTest, HeaderOnlyThenPayload) {
  // Arrange: 3-byte payload, no secondary header -> total length = 6 + 3 = 9
  std::vector<std::uint8_t> payload{0xDE, 0xAD, 0xBE};
  auto packet = makePacket(/*ver*/ 1, /*type*/ false, /*APID*/ 0x033, /*seqFlags*/ 0,
                           /*seqCount*/ 7, payload);
  ASSERT_EQ(packet.size(), SPP_HDR_SIZE_BYTES + payload.size());

  ProcessorDefault proc;
  std::vector<std::vector<std::uint8_t>> captured;
  CaptureCtx ctx{&captured};
  proc.setPacketCallback(captureCallback, &ctx);

  // Act 1: feed exactly the 6-byte primary header
  auto r1 = proc.process(apex::compat::bytes_span{packet.data(), SPP_HDR_SIZE_BYTES});
  // Assert: header is present but payload missing -> NEED_MORE, no packet yet
  EXPECT_EQ(r1.status, Status::NEED_MORE);
  EXPECT_EQ(r1.packetsExtracted, 0u);
  EXPECT_EQ(captured.size(), 0u);
  EXPECT_EQ(proc.bufferedSize(), SPP_HDR_SIZE_BYTES);

  // Act 2: feed the remaining payload bytes
  auto r2 = proc.process(apex::compat::bytes_span{packet.data() + SPP_HDR_SIZE_BYTES,
                                                  packet.size() - SPP_HDR_SIZE_BYTES});
  // Assert: full packet should now extract
  EXPECT_EQ(r2.status, Status::OK);
  EXPECT_EQ(r2.packetsExtracted, 1u);
  ASSERT_EQ(captured.size(), 1u);
  EXPECT_EQ(captured[0], packet);
  EXPECT_EQ(proc.bufferedSize(), 0u);
}

/**
 * @test Multiple consecutive oversize "headers" followed by a valid packet.
 *       Verifies repeated WARNING_DESYNC_DROPPED resync until a good header is found,
 *       then a successful extraction in the same process() call.
 */
TEST(SppProcessorTest, MultipleOversizeThenValidPacket) {
  // Build a 6-byte "header" with PD > max to force oversize.
  std::vector<std::uint8_t> badHeader(6, 0x00);
  badHeader[0] = static_cast<std::uint8_t>((1u & SPP_VERSION_MASK) << SPP_VERSION_SHIFT);
  const std::uint16_t PD_TOO_BIG = static_cast<std::uint16_t>(MAX_SPP_PACKET_LENGTH);
  badHeader[4] = static_cast<std::uint8_t>((PD_TOO_BIG >> 8) & 0xFF);
  badHeader[5] = static_cast<std::uint8_t>(PD_TOO_BIG & 0xFF);

  // Chain several bad headers back-to-back, then a valid packet.
  std::vector<std::uint8_t> payload{0xAA, 0xBB, 0xCC, 0xDD};
  auto goodPacket = makePacket(0, true, 0x055, 3, 123, payload);

  std::vector<std::uint8_t> stream;
  // Prepend three oversize headers (18 bytes junk)
  for (int i = 0; i < 3; ++i) {
    stream.insert(stream.end(), badHeader.begin(), badHeader.end());
  }
  // Follow with a valid packet
  stream.insert(stream.end(), goodPacket.begin(), goodPacket.end());

  ProcessorDefault proc; // default: dropUntilValidHeader=true
  std::vector<std::vector<std::uint8_t>> captured;
  CaptureCtx ctx{&captured};
  proc.setPacketCallback(captureCallback, &ctx);

  // Act
  auto r = proc.process(apex::compat::bytes_span{stream.data(), stream.size()});

  // Assert: at least 3*6 = 18 bytes dropped one-by-one, then 1 good packet extracted
  EXPECT_GE(r.resyncDrops, 18u);
  EXPECT_EQ(r.packetsExtracted, 1u);
  EXPECT_EQ(r.status, Status::OK); // extraction took place
  ASSERT_EQ(captured.size(), 1u);
  EXPECT_EQ(captured[0], goodPacket);
  EXPECT_EQ(proc.bufferedSize(), 0u);
}

/**
 * @test No callback installed: packets are still counted and consumed
 *       (verifies the "count only" path without delivery).
 */
TEST(SppProcessorTest, CountsWithoutCallback) {
  // Build two small valid packets back-to-back
  auto p1 = makePacket(0, false, 0x100, 0, 1, std::vector<std::uint8_t>{0x01});
  auto p2 = makePacket(0, false, 0x101, 1, 2, std::vector<std::uint8_t>{0x02, 0x03});

  std::vector<std::uint8_t> stream;
  stream.insert(stream.end(), p1.begin(), p1.end());
  stream.insert(stream.end(), p2.begin(), p2.end());

  ProcessorDefault proc; // no setPacketCallback called

  // Act
  auto r = proc.process(apex::compat::bytes_span{stream.data(), stream.size()});

  // Assert: both packets counted & buffer drained, without any callback side-effects
  EXPECT_EQ(r.status, Status::OK);
  EXPECT_EQ(r.packetsExtracted, 2u);
  EXPECT_EQ(r.resyncDrops, 0u);
  EXPECT_EQ(proc.bufferedSize(), 0u);
}

/**
 * @test Packet whose total length equals the configured maxPacketLength is accepted.
 *       (Boundary test for the length guard: total = 6 + (PD+1) == MAX_LEN.)
 *
 * For no secondary header:
 *   total = 6 + payloadLen
 *   PD    = payloadLen - 1
 * To make total == MAX_LEN, choose payloadLen = MAX_LEN - 6.
 */
TEST(SppProcessorTest, AcceptsPacketAtConfiguredMax) {
  constexpr std::size_t MAX_LEN = 25;

  // Payload must be MAX_LEN - 6 bytes to make total == MAX_LEN.
  const std::size_t PAYLOAD_LEN = MAX_LEN - SPP_HDR_SIZE_BYTES; // == 19 for MAX_LEN=25
  ASSERT_EQ(SPP_HDR_SIZE_BYTES + PAYLOAD_LEN, MAX_LEN);

  std::vector<std::uint8_t> payload(PAYLOAD_LEN, 0x5A);
  auto packet = makePacket(/*ver*/ 1, /*type*/ false, /*APID*/ 0x020, /*seqFlags*/ 0,
                           /*seqCount*/ 7, payload);
  ASSERT_EQ(packet.size(), MAX_LEN);

  ProcessorDefault proc;
  ProcessorConfig cfg = proc.config();
  cfg.maxPacketLength = MAX_LEN; // tighten guard to the exact total we crafted
  proc.setConfig(cfg);

  std::vector<std::vector<std::uint8_t>> captured;
  CaptureCtx ctx{&captured};
  proc.setPacketCallback(captureCallback, &ctx);

  auto r = proc.process(apex::compat::bytes_span{packet.data(), packet.size()});
  EXPECT_EQ(r.status, Status::OK);
  EXPECT_EQ(r.packetsExtracted, 1u);
  ASSERT_EQ(captured.size(), 1u);
  EXPECT_EQ(captured[0], packet);
  EXPECT_EQ(proc.bufferedSize(), 0u);
}

/**
 * @test Oversize header with dropUntilValidHeader=false holds bytes and reports
 * ERROR_LENGTH_OVER_MAX; then flipping dropUntilValidHeader=true and feeding a valid packet allows
 * resync+extraction in the same call (bad header remains at front of buffer from previous step).
 */
TEST(SppProcessorTest, NoDropThenEnableDropAndResyncOverBadHeader) {
  // Build a single oversize "header" (PD causes total > max).
  std::vector<std::uint8_t> badHeader(6, 0x00);
  badHeader[0] = static_cast<std::uint8_t>((1u & SPP_VERSION_MASK) << SPP_VERSION_SHIFT);
  const std::uint16_t PD_TOO_BIG = static_cast<std::uint16_t>(MAX_SPP_PACKET_LENGTH);
  badHeader[4] = static_cast<std::uint8_t>((PD_TOO_BIG >> 8) & 0xFF);
  badHeader[5] = static_cast<std::uint8_t>(PD_TOO_BIG & 0xFF);

  // Prepare a valid packet we'll append later.
  std::vector<std::uint8_t> payload{0xCA, 0xFE, 0xBA, 0xBE};
  auto goodPacket =
      makePacket(/*ver*/ 0, /*type*/ true, /*APID*/ 0x031, /*seqFlags*/ 1, /*seqCount*/ 9, payload);

  ProcessorDefault proc;

  // First, disable dropping: we should get a hard error and the header should remain buffered.
  {
    ProcessorConfig cfg = proc.config();
    cfg.dropUntilValidHeader = false;
    proc.setConfig(cfg);
    auto r = proc.process(apex::compat::bytes_span{badHeader.data(), badHeader.size()});
    EXPECT_EQ(r.status, Status::ERROR_LENGTH_OVER_MAX);
    EXPECT_EQ(r.packetsExtracted, 0u);
    EXPECT_EQ(r.resyncDrops, 0u);
    EXPECT_EQ(proc.bufferedSize(), badHeader.size()); // still buffered
  }

  // Now enable dropping and feed a valid packet; processor should drop the bad header
  // and extract the following good packet in the same call.
  {
    ProcessorConfig cfg = proc.config();
    cfg.dropUntilValidHeader = true;
    proc.setConfig(cfg);

    std::vector<std::vector<std::uint8_t>> captured;
    CaptureCtx ctx{&captured};
    proc.setPacketCallback(captureCallback, &ctx);

    auto r = proc.process(apex::compat::bytes_span{goodPacket.data(), goodPacket.size()});
    EXPECT_GE(r.resyncDrops, SPP_HDR_SIZE_BYTES); // dropped the stale bad header
    EXPECT_EQ(r.packetsExtracted, 1u);
    EXPECT_EQ(r.status, Status::OK);
    ASSERT_EQ(captured.size(), 1u);
    EXPECT_EQ(captured[0], goodPacket);
    EXPECT_EQ(proc.bufferedSize(), 0u);
  }
}

/**
 * @test Verify counters are accumulated correctly across multiple process() calls.
 */
TEST(SppProcessorTest, CountersAccumulation) {
  auto p1 = makePacket(0, false, 0x100, 0, 1, std::vector<std::uint8_t>{0x01});
  auto p2 = makePacket(0, false, 0x101, 0, 2, std::vector<std::uint8_t>{0x02});

  ProcessorDefault proc;

  auto r1 = proc.process(apex::compat::bytes_span{p1.data(), p1.size()});
  EXPECT_EQ(r1.packetsExtracted, 1u);

  auto r2 = proc.process(apex::compat::bytes_span{p2.data(), p2.size()});
  EXPECT_EQ(r2.packetsExtracted, 1u);

  auto counters = proc.counters();
  EXPECT_EQ(counters.totalPacketsExtracted, 2u);
  EXPECT_EQ(counters.totalBytesIn, p1.size() + p2.size());
  EXPECT_EQ(counters.totalCalls, 2u);

  proc.resetCounters();
  auto after = proc.counters();
  EXPECT_EQ(after.totalPacketsExtracted, 0u);
  EXPECT_EQ(after.totalBytesIn, 0u);
  EXPECT_EQ(after.totalCalls, 0u);
}

/**
 * @test Verify buffer full status when input exceeds fixed buffer capacity.
 */
TEST(SppProcessorTest, BufferFullStatus) {
  // Use a small processor to make it easy to fill
  Processor<64> proc;

  // Create data larger than buffer capacity
  std::vector<std::uint8_t> bigData(100, 0xAB);

  auto r = proc.process(apex::compat::bytes_span{bigData.data(), bigData.size()});
  EXPECT_EQ(r.status, Status::ERROR_BUFFER_FULL);
  EXPECT_LT(r.bytesConsumed, bigData.size()); // Not all bytes fit
}

/**
 * @test Verify toString() for all Status values.
 */
TEST(SppProcessorTest, StatusToString) {
  EXPECT_STREQ(toString(Status::OK), "OK");
  EXPECT_STREQ(toString(Status::NEED_MORE), "NEED_MORE");
  EXPECT_STREQ(toString(Status::WARNING_DESYNC_DROPPED), "WARNING_DESYNC_DROPPED");
  EXPECT_STREQ(toString(Status::ERROR_LENGTH_OVER_MAX), "ERROR_LENGTH_OVER_MAX");
  EXPECT_STREQ(toString(Status::ERROR_BUFFER_FULL), "ERROR_BUFFER_FULL");
}
