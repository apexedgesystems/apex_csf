/**
 * @file AprotoProcessor_uTest.cpp
 * @brief Unit tests for APROTO streaming packet extractor.
 *
 * Tests sliding-window extraction, partial packet handling, resync on
 * invalid magic, and callback delivery.
 */

#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoProcessor.hpp"
#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoCodec.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

using system_core::protocols::aproto::APROTO_CRC_SIZE;
using system_core::protocols::aproto::APROTO_HEADER_SIZE;
using system_core::protocols::aproto::APROTO_MAGIC;
using system_core::protocols::aproto::buildHeader;
using system_core::protocols::aproto::encodePacket;
using system_core::protocols::aproto::HighWatermarkDelegate;
using system_core::protocols::aproto::PacketDelegate;
using system_core::protocols::aproto::ProcessorConfig;
using system_core::protocols::aproto::ProcessorCounters;
using system_core::protocols::aproto::ProcessorDefault;
using system_core::protocols::aproto::ProcessorSmall;
using system_core::protocols::aproto::ProcessorStatus;
using system_core::protocols::aproto::ProcessResult;
using system_core::protocols::aproto::toString;

/* ----------------------------- Helpers ----------------------------- */

namespace {

/// Build a valid APROTO packet with given payload.
std::vector<std::uint8_t> makePacket(std::uint32_t fullUid, std::uint16_t opcode, std::uint16_t seq,
                                     const std::vector<std::uint8_t>& payload, bool crc = false) {
  auto hdr = buildHeader(fullUid, opcode, seq, static_cast<std::uint16_t>(payload.size()), false,
                         false, crc);
  const std::size_t totalSize = APROTO_HEADER_SIZE + payload.size() + (crc ? APROTO_CRC_SIZE : 0);
  std::vector<std::uint8_t> buf(totalSize);
  std::size_t written = 0;
  (void)encodePacket(hdr, {payload.data(), payload.size()}, {buf.data(), buf.size()}, written);
  buf.resize(written);
  return buf;
}

/// Callback context for collecting packets.
struct CollectorCtx {
  std::vector<std::vector<std::uint8_t>> packets;
};

void collectPacket(void* ctx, apex::compat::bytes_span pkt) noexcept {
  auto* c = static_cast<CollectorCtx*>(ctx);
  c->packets.emplace_back(pkt.data(), pkt.data() + pkt.size());
}

/// High watermark callback context.
struct WatermarkCtx {
  std::size_t lastWatermark = 0;
  int callCount = 0;
};

void onWatermark(void* ctx, std::size_t bytes) noexcept {
  auto* c = static_cast<WatermarkCtx*>(ctx);
  c->lastWatermark = bytes;
  ++c->callCount;
}

} // namespace

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default-constructed processor has zero buffered bytes. */
TEST(AprotoProcessorTest, DefaultConstruction) {
  ProcessorDefault proc;
  EXPECT_EQ(proc.bufferedSize(), 0u);
  EXPECT_EQ(ProcessorDefault::capacity(), 8192u);
}

/** @test Small processor has 4KB capacity. */
TEST(AprotoProcessorTest, SmallCapacity) { EXPECT_EQ(ProcessorSmall::capacity(), 4096u); }

/* ----------------------------- Single Packet ----------------------------- */

/** @test Extract a single complete packet delivered in one chunk. */
TEST(AprotoProcessorTest, SinglePacketComplete) {
  ProcessorDefault proc;
  CollectorCtx ctx;
  proc.setPacketCallback(collectPacket, &ctx);

  auto pkt = makePacket(0x010200, 0x0001, 1, {0xAA, 0xBB, 0xCC, 0xDD});
  auto r = proc.process({pkt.data(), pkt.size()});

  EXPECT_EQ(r.status, ProcessorStatus::OK);
  EXPECT_EQ(r.packetsExtracted, 1u);
  EXPECT_EQ(r.bytesConsumed, pkt.size());
  EXPECT_EQ(r.resyncDrops, 0u);
  ASSERT_EQ(ctx.packets.size(), 1u);
  EXPECT_EQ(ctx.packets[0].size(), pkt.size());
  EXPECT_EQ(ctx.packets[0], pkt);
}

/* ----------------------------- Partial Packets ----------------------------- */

/** @test Extract a packet delivered in two partial chunks. */
TEST(AprotoProcessorTest, PartialPacketTwoChunks) {
  ProcessorDefault proc;
  CollectorCtx ctx;
  proc.setPacketCallback(collectPacket, &ctx);

  auto pkt = makePacket(0x010200, 0x0002, 2, {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08});

  // Send first half (partial header + some payload).
  const std::size_t split = pkt.size() / 2;
  auto r1 = proc.process({pkt.data(), split});
  EXPECT_EQ(r1.packetsExtracted, 0u);
  EXPECT_EQ(ctx.packets.size(), 0u);

  // Send second half.
  auto r2 = proc.process({pkt.data() + split, pkt.size() - split});
  EXPECT_EQ(r2.status, ProcessorStatus::OK);
  EXPECT_EQ(r2.packetsExtracted, 1u);
  ASSERT_EQ(ctx.packets.size(), 1u);
  EXPECT_EQ(ctx.packets[0], pkt);
}

/** @test NEED_MORE when only partial header is available. */
TEST(AprotoProcessorTest, PartialHeaderNeedMore) {
  ProcessorDefault proc;

  auto pkt = makePacket(0x010200, 0x0001, 1, {0xAA});

  // Send only 6 bytes (less than 14-byte header).
  auto r = proc.process({pkt.data(), 6});
  EXPECT_EQ(r.status, ProcessorStatus::NEED_MORE);
  EXPECT_EQ(r.packetsExtracted, 0u);
  EXPECT_EQ(proc.bufferedSize(), 6u);
}

/* ----------------------------- Multiple Packets ----------------------------- */

/** @test Extract multiple packets concatenated in one chunk. */
TEST(AprotoProcessorTest, MultiplePacketsOneChunk) {
  ProcessorDefault proc;
  CollectorCtx ctx;
  proc.setPacketCallback(collectPacket, &ctx);

  auto pkt1 = makePacket(0x010200, 0x0001, 1, {0xAA});
  auto pkt2 = makePacket(0x020300, 0x0002, 2, {0xBB, 0xCC});
  auto pkt3 = makePacket(0x030400, 0x0003, 3, {0xDD, 0xEE, 0xFF});

  std::vector<std::uint8_t> combined;
  combined.insert(combined.end(), pkt1.begin(), pkt1.end());
  combined.insert(combined.end(), pkt2.begin(), pkt2.end());
  combined.insert(combined.end(), pkt3.begin(), pkt3.end());

  auto r = proc.process({combined.data(), combined.size()});
  EXPECT_EQ(r.status, ProcessorStatus::OK);
  EXPECT_EQ(r.packetsExtracted, 3u);
  ASSERT_EQ(ctx.packets.size(), 3u);
  EXPECT_EQ(ctx.packets[0], pkt1);
  EXPECT_EQ(ctx.packets[1], pkt2);
  EXPECT_EQ(ctx.packets[2], pkt3);
}

/* ----------------------------- Resync ----------------------------- */

/** @test Garbage bytes before valid packet are dropped and resync occurs. */
TEST(AprotoProcessorTest, ResyncOnGarbage) {
  ProcessorDefault proc;
  CollectorCtx ctx;
  proc.setPacketCallback(collectPacket, &ctx);

  auto pkt = makePacket(0x010200, 0x0001, 1, {0xAA, 0xBB});

  // Prepend 5 garbage bytes.
  std::vector<std::uint8_t> combined = {0xFF, 0xFE, 0xFD, 0xFC, 0xFB};
  combined.insert(combined.end(), pkt.begin(), pkt.end());

  auto r = proc.process({combined.data(), combined.size()});
  EXPECT_EQ(r.status, ProcessorStatus::OK);
  EXPECT_EQ(r.packetsExtracted, 1u);
  EXPECT_EQ(r.resyncDrops, 5u);
  ASSERT_EQ(ctx.packets.size(), 1u);
  EXPECT_EQ(ctx.packets[0], pkt);
}

/** @test Resync drops are counted correctly in counters. */
TEST(AprotoProcessorTest, ResyncCountersAccumulate) {
  ProcessorDefault proc;
  CollectorCtx ctx;
  proc.setPacketCallback(collectPacket, &ctx);

  auto pkt = makePacket(0x010200, 0x0001, 1, {0x11});

  // Two chunks with garbage.
  std::vector<std::uint8_t> chunk1 = {0xFF, 0xFE};
  chunk1.insert(chunk1.end(), pkt.begin(), pkt.end());
  (void)proc.process({chunk1.data(), chunk1.size()});

  std::vector<std::uint8_t> chunk2 = {0xFD};
  chunk2.insert(chunk2.end(), pkt.begin(), pkt.end());
  (void)proc.process({chunk2.data(), chunk2.size()});

  auto c = proc.counters();
  EXPECT_EQ(c.totalPacketsExtracted, 2u);
  EXPECT_EQ(c.totalResyncDrops, 3u);
  EXPECT_EQ(c.totalCalls, 2u);
}

/* ----------------------------- CRC Packets ----------------------------- */

/** @test Extract packet with CRC trailer. */
TEST(AprotoProcessorTest, PacketWithCrc) {
  ProcessorDefault proc;
  CollectorCtx ctx;
  proc.setPacketCallback(collectPacket, &ctx);

  auto pkt = makePacket(0x010200, 0x0001, 1, {0xAA, 0xBB, 0xCC, 0xDD}, true);
  auto r = proc.process({pkt.data(), pkt.size()});

  EXPECT_EQ(r.status, ProcessorStatus::OK);
  EXPECT_EQ(r.packetsExtracted, 1u);
  ASSERT_EQ(ctx.packets.size(), 1u);
  EXPECT_EQ(ctx.packets[0], pkt);
}

/* ----------------------------- Empty Input ----------------------------- */

/** @test Empty input returns OK with no packets. */
TEST(AprotoProcessorTest, EmptyInput) {
  ProcessorDefault proc;
  auto r = proc.process({});
  EXPECT_EQ(r.status, ProcessorStatus::OK);
  EXPECT_EQ(r.packetsExtracted, 0u);
  EXPECT_EQ(r.bytesConsumed, 0u);
}

/* ----------------------------- Zero-Length Payload ----------------------------- */

/** @test Header-only packet (zero payload) is extracted. */
TEST(AprotoProcessorTest, ZeroPayloadPacket) {
  ProcessorDefault proc;
  CollectorCtx ctx;
  proc.setPacketCallback(collectPacket, &ctx);

  auto pkt = makePacket(0x010200, 0x0001, 1, {});
  EXPECT_EQ(pkt.size(), APROTO_HEADER_SIZE);

  auto r = proc.process({pkt.data(), pkt.size()});
  EXPECT_EQ(r.status, ProcessorStatus::OK);
  EXPECT_EQ(r.packetsExtracted, 1u);
  ASSERT_EQ(ctx.packets.size(), 1u);
  EXPECT_EQ(ctx.packets[0].size(), APROTO_HEADER_SIZE);
}

/* ----------------------------- Buffer Full ----------------------------- */

/** @test Buffer full is reported when input exceeds capacity. */
TEST(AprotoProcessorTest, BufferFull) {
  system_core::protocols::aproto::Processor<64> proc;

  // Try to push more data than the 64-byte buffer can hold.
  std::vector<std::uint8_t> large(128, 0xFF);
  auto r = proc.process({large.data(), large.size()});
  EXPECT_EQ(r.status, ProcessorStatus::ERROR_BUFFER_FULL);
  EXPECT_LT(r.bytesConsumed, large.size());
}

/* ----------------------------- Reset ----------------------------- */

/** @test Reset clears buffered state. */
TEST(AprotoProcessorTest, ResetClearsBuffer) {
  ProcessorDefault proc;

  auto pkt = makePacket(0x010200, 0x0001, 1, {0xAA});

  // Send partial.
  (void)proc.process({pkt.data(), 6});
  EXPECT_GT(proc.bufferedSize(), 0u);

  proc.reset();
  EXPECT_EQ(proc.bufferedSize(), 0u);
}

/* ----------------------------- No Callback ----------------------------- */

/** @test Packets are counted but not delivered when no callback is set. */
TEST(AprotoProcessorTest, NoCallbackStillCounts) {
  ProcessorDefault proc;

  auto pkt = makePacket(0x010200, 0x0001, 1, {0xAA, 0xBB});
  auto r = proc.process({pkt.data(), pkt.size()});

  EXPECT_EQ(r.status, ProcessorStatus::OK);
  EXPECT_EQ(r.packetsExtracted, 1u);
  EXPECT_EQ(proc.counters().totalPacketsExtracted, 1u);
}

/* ----------------------------- Config ----------------------------- */

/** @test Config can be updated between process() calls. */
TEST(AprotoProcessorTest, ConfigRoundTrip) {
  ProcessorDefault proc;

  ProcessorConfig cfg;
  cfg.maxPacketLength = 256;
  cfg.compactThreshold = 512;
  proc.setConfig(cfg);

  auto got = proc.config();
  EXPECT_EQ(got.maxPacketLength, 256u);
  EXPECT_EQ(got.compactThreshold, 512u);
}

/* ----------------------------- High Watermark ----------------------------- */

/** @test High watermark callback fires when threshold is crossed. */
TEST(AprotoProcessorTest, HighWatermarkCallback) {
  ProcessorDefault proc;
  WatermarkCtx wmCtx;

  ProcessorConfig cfg;
  cfg.highWatermarkBytes = 20;
  cfg.highWatermarkCallback = HighWatermarkDelegate{onWatermark, &wmCtx};
  proc.setConfig(cfg);

  // Push enough data to cross watermark.
  auto pkt = makePacket(0x010200, 0x0001, 1, {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08});
  (void)proc.process({pkt.data(), pkt.size()});

  // Packet is extracted, so buffer is drained. Watermark fires during append phase.
  // The packet is 22 bytes (14 header + 8 payload), which crosses the 20-byte threshold.
  EXPECT_GE(wmCtx.callCount, 1);
}

/* ----------------------------- Counter Reset ----------------------------- */

/** @test Counter reset zeroes all running totals. */
TEST(AprotoProcessorTest, CounterReset) {
  ProcessorDefault proc;

  auto pkt = makePacket(0x010200, 0x0001, 1, {0xAA});
  (void)proc.process({pkt.data(), pkt.size()});

  EXPECT_GT(proc.counters().totalPacketsExtracted, 0u);

  proc.resetCounters();
  auto c = proc.counters();
  EXPECT_EQ(c.totalBytesIn, 0u);
  EXPECT_EQ(c.totalPacketsExtracted, 0u);
  EXPECT_EQ(c.totalResyncDrops, 0u);
  EXPECT_EQ(c.totalCalls, 0u);
}

/* ----------------------------- toString ----------------------------- */

/** @test toString returns non-null for all ProcessorStatus values. */
TEST(AprotoProcessorTest, ToStringCoverage) {
  EXPECT_STREQ(toString(ProcessorStatus::OK), "OK");
  EXPECT_STREQ(toString(ProcessorStatus::NEED_MORE), "NEED_MORE");
  EXPECT_STREQ(toString(ProcessorStatus::WARNING_DESYNC_DROPPED), "WARNING_DESYNC_DROPPED");
  EXPECT_STREQ(toString(ProcessorStatus::ERROR_LENGTH_OVER_MAX), "ERROR_LENGTH_OVER_MAX");
  EXPECT_STREQ(toString(ProcessorStatus::ERROR_BUFFER_FULL), "ERROR_BUFFER_FULL");
  EXPECT_STREQ(toString(static_cast<ProcessorStatus>(255)), "UNKNOWN");
}

/* ----------------------------- Byte-at-a-time ----------------------------- */

/** @test Feeding bytes one at a time still extracts the packet. */
TEST(AprotoProcessorTest, ByteAtATime) {
  ProcessorDefault proc;
  CollectorCtx ctx;
  proc.setPacketCallback(collectPacket, &ctx);

  auto pkt = makePacket(0x010200, 0x0001, 1, {0xAA, 0xBB, 0xCC});

  for (std::size_t i = 0; i < pkt.size(); ++i) {
    (void)proc.process({pkt.data() + i, 1});
  }

  ASSERT_EQ(ctx.packets.size(), 1u);
  EXPECT_EQ(ctx.packets[0], pkt);
}
