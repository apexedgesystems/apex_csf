/**
 * @file CcsdsEppProcessor_uTest.cpp
 * @brief Unit tests for CCSDS EPP Processor (RT-safe streaming extractor).
 *
 * Reference: CCSDS 133.1-B-3 "Encapsulation Packet Protocol" Blue Book
 */

#include "src/system/core/infrastructure/protocols/ccsds/epp/inc/CcsdsEppProcessor.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using namespace protocols::ccsds::epp;

/* ----------------------------- Helper Functions ---------------------------- */

namespace {

/// @brief Build 1-octet header (idle packet, LoL=00).
std::vector<std::uint8_t> buildEppHeader1(std::uint8_t version, std::uint8_t protocolId) {
  const std::uint8_t BYTE0 =
      static_cast<std::uint8_t>(((version & 0x07) << 5) | ((protocolId & 0x07) << 2) | 0x00);
  return {BYTE0};
}

/// @brief Build 2-octet header (LoL=01), totalLength stored in 1 byte.
std::vector<std::uint8_t> buildEppHeader2(std::uint8_t version, std::uint8_t protocolId,
                                          std::uint8_t totalLength) {
  const std::uint8_t BYTE0 =
      static_cast<std::uint8_t>(((version & 0x07) << 5) | ((protocolId & 0x07) << 2) | 0x01);
  return {BYTE0, totalLength};
}

/// @brief Build 4-octet header (LoL=10), totalLength stored in 2 bytes (big-endian).
std::vector<std::uint8_t> buildEppHeader4(std::uint8_t version, std::uint8_t protocolId,
                                          std::uint8_t userDefined, std::uint8_t protocolIde,
                                          std::uint16_t totalLength) {
  const std::uint8_t BYTE0 =
      static_cast<std::uint8_t>(((version & 0x07) << 5) | ((protocolId & 0x07) << 2) | 0x02);
  const std::uint8_t BYTE1 =
      static_cast<std::uint8_t>(((userDefined & 0x0F) << 4) | (protocolIde & 0x0F));
  const std::uint8_t BYTE2 = static_cast<std::uint8_t>((totalLength >> 8) & 0xFF);
  const std::uint8_t BYTE3 = static_cast<std::uint8_t>(totalLength & 0xFF);
  return {BYTE0, BYTE1, BYTE2, BYTE3};
}

/// @brief Assemble EPP packet from header bytes and payload.
std::vector<std::uint8_t> createEppPacket(const std::vector<std::uint8_t>& headerBytes,
                                          const std::vector<std::uint8_t>& payload) {
  std::vector<std::uint8_t> packet;
  packet.insert(packet.end(), headerBytes.begin(), headerBytes.end());
  packet.insert(packet.end(), payload.begin(), payload.end());
  return packet;
}

/// @brief Test context for callback-based packet collection.
struct PacketCollector {
  std::vector<std::vector<std::uint8_t>> packets;

  static void callback(void* ctx, apex::compat::bytes_span pkt) noexcept {
    auto* self = static_cast<PacketCollector*>(ctx);
    self->packets.emplace_back(pkt.begin(), pkt.end());
  }
};

} // namespace

/* ----------------------------- Default Construction ------------------------ */

/** @test Verify default construction creates empty processor. */
TEST(EppProcessorTest, DefaultConstruction) {
  ProcessorDefault proc;
  EXPECT_EQ(proc.bufferedSize(), 0U);
  EXPECT_EQ(proc.capacity(), 8192U);

  auto counters = proc.counters();
  EXPECT_EQ(counters.totalBytesIn, 0U);
  EXPECT_EQ(counters.totalPacketsExtracted, 0U);
}

/* ----------------------------- Single Packet Tests ------------------------- */

/** @test Extract single packet with 2-octet header. */
TEST(EppProcessorTest, SinglePacketExtraction) {
  std::vector<std::uint8_t> payload = {0x20, 0x30};
  const std::uint8_t TOTAL_LEN = static_cast<std::uint8_t>(2 + payload.size());
  std::vector<std::uint8_t> header = buildEppHeader2(7, 5, TOTAL_LEN);
  std::vector<std::uint8_t> packet = createEppPacket(header, payload);
  EXPECT_EQ(packet.size(), 4U);

  PacketCollector collector;
  ProcessorDefault proc;
  proc.setPacketCallback(&PacketCollector::callback, &collector);

  ProcessResult r = proc.process(apex::compat::bytes_span{packet.data(), packet.size()});

  EXPECT_EQ(r.status, Status::OK);
  EXPECT_EQ(r.packetsExtracted, 1U);
  EXPECT_EQ(r.bytesConsumed, 4U);
  EXPECT_EQ(collector.packets.size(), 1U);
  EXPECT_EQ(collector.packets[0], packet);
}

/** @test Extract idle packet (1-octet header). */
TEST(EppProcessorTest, IdlePacketExtraction) {
  std::vector<std::uint8_t> packet = buildEppHeader1(7, 0);
  EXPECT_EQ(packet.size(), 1U);

  PacketCollector collector;
  ProcessorDefault proc;
  proc.setPacketCallback(&PacketCollector::callback, &collector);

  ProcessResult r = proc.process(apex::compat::bytes_span{packet.data(), packet.size()});

  EXPECT_EQ(r.status, Status::OK);
  EXPECT_EQ(r.packetsExtracted, 1U);
  EXPECT_EQ(collector.packets.size(), 1U);
  EXPECT_EQ(collector.packets[0], packet);
}

/* ----------------------------- Partial Packet Tests ------------------------ */

/** @test Partial packet extraction across multiple process() calls. */
TEST(EppProcessorTest, PartialPacketExtraction) {
  std::vector<std::uint8_t> payload = {0x10, 0x11, 0x12};
  const std::uint8_t TOTAL_LEN = static_cast<std::uint8_t>(2 + payload.size());
  std::vector<std::uint8_t> header = buildEppHeader2(7, 2, TOTAL_LEN);
  std::vector<std::uint8_t> packet = createEppPacket(header, payload);
  EXPECT_EQ(packet.size(), 5U);

  PacketCollector collector;
  ProcessorDefault proc;
  proc.setPacketCallback(&PacketCollector::callback, &collector);

  // Split packet into two parts.
  const std::size_t SPLIT_IDX = 3;
  std::vector<std::uint8_t> part1(packet.begin(), packet.begin() + SPLIT_IDX);
  std::vector<std::uint8_t> part2(packet.begin() + SPLIT_IDX, packet.end());

  // First part - not enough for complete packet.
  ProcessResult r1 = proc.process(apex::compat::bytes_span{part1.data(), part1.size()});
  EXPECT_EQ(r1.status, Status::NEED_MORE);
  EXPECT_EQ(r1.packetsExtracted, 0U);
  EXPECT_EQ(collector.packets.size(), 0U);

  // Second part - completes the packet.
  ProcessResult r2 = proc.process(apex::compat::bytes_span{part2.data(), part2.size()});
  EXPECT_EQ(r2.status, Status::OK);
  EXPECT_EQ(r2.packetsExtracted, 1U);
  EXPECT_EQ(collector.packets.size(), 1U);
  EXPECT_EQ(collector.packets[0], packet);
}

/* ----------------------------- Multiple Packet Tests ----------------------- */

/** @test Extract multiple packets from single stream. */
TEST(EppProcessorTest, MultiplePacketsExtraction) {
  // Packet 1: 2-octet header.
  std::vector<std::uint8_t> payload1 = {0x11, 0x22};
  const std::uint8_t TOTAL_LEN_1 = static_cast<std::uint8_t>(2 + payload1.size());
  std::vector<std::uint8_t> header1 = buildEppHeader2(7, 3, TOTAL_LEN_1);
  std::vector<std::uint8_t> packet1 = createEppPacket(header1, payload1);

  // Packet 2: 2-octet header.
  std::vector<std::uint8_t> payload2 = {0x33, 0x44, 0x55};
  const std::uint8_t TOTAL_LEN_2 = static_cast<std::uint8_t>(2 + payload2.size());
  std::vector<std::uint8_t> header2 = buildEppHeader2(7, 2, TOTAL_LEN_2);
  std::vector<std::uint8_t> packet2 = createEppPacket(header2, payload2);

  // Concatenate both packets.
  std::vector<std::uint8_t> stream;
  stream.insert(stream.end(), packet1.begin(), packet1.end());
  stream.insert(stream.end(), packet2.begin(), packet2.end());

  PacketCollector collector;
  ProcessorDefault proc;
  proc.setPacketCallback(&PacketCollector::callback, &collector);

  ProcessResult r = proc.process(apex::compat::bytes_span{stream.data(), stream.size()});

  EXPECT_EQ(r.status, Status::OK);
  EXPECT_EQ(r.packetsExtracted, 2U);
  EXPECT_EQ(collector.packets.size(), 2U);
  EXPECT_EQ(collector.packets[0], packet1);
  EXPECT_EQ(collector.packets[1], packet2);
}

/* ----------------------------- Reset Tests --------------------------------- */

/** @test Verify reset() clears buffer and allows reuse. */
TEST(EppProcessorTest, ResetClearsBuffer) {
  std::vector<std::uint8_t> payload = {0x99, 0x88};
  const std::uint8_t TOTAL_LEN = static_cast<std::uint8_t>(2 + payload.size());
  std::vector<std::uint8_t> header = buildEppHeader2(7, 4, TOTAL_LEN);
  std::vector<std::uint8_t> packet = createEppPacket(header, payload);

  PacketCollector collector;
  ProcessorDefault proc;
  proc.setPacketCallback(&PacketCollector::callback, &collector);

  (void)proc.process(apex::compat::bytes_span{packet.data(), packet.size()});
  EXPECT_EQ(collector.packets.size(), 1U);

  proc.reset();
  collector.packets.clear();
  EXPECT_EQ(proc.bufferedSize(), 0U);

  // Process same packet again.
  (void)proc.process(apex::compat::bytes_span{packet.data(), packet.size()});
  EXPECT_EQ(collector.packets.size(), 1U);
}

/* ----------------------------- Resync Tests -------------------------------- */

/** @test Verify resync drops bytes on invalid version. */
TEST(EppProcessorTest, ResyncOnInvalidVersion) {
  // Invalid version (6 instead of 7).
  const std::uint8_t INVALID_BYTE = (6 << 5) | (0 << 2) | 0x00;
  // Valid idle packet after.
  const std::uint8_t VALID_IDLE = (7 << 5) | (0 << 2) | 0x00;
  std::vector<std::uint8_t> stream = {INVALID_BYTE, VALID_IDLE};

  PacketCollector collector;
  ProcessorDefault proc;
  proc.setPacketCallback(&PacketCollector::callback, &collector);

  ProcessResult r = proc.process(apex::compat::bytes_span{stream.data(), stream.size()});

  // Should have dropped 1 byte and extracted 1 packet.
  EXPECT_EQ(r.packetsExtracted, 1U);
  EXPECT_EQ(r.resyncDrops, 1U);
  EXPECT_EQ(collector.packets.size(), 1U);
}

/** @test Verify resync on invalid packet length. */
TEST(EppProcessorTest, ResyncOnInvalidLength) {
  // 4-octet header with invalid total length (2, less than header length of 4).
  const std::uint16_t INVALID_LEN = 2;
  std::vector<std::uint8_t> header = buildEppHeader4(7, 1, 0, 0, INVALID_LEN);

  PacketCollector collector;
  ProcessorDefault proc;
  proc.setPacketCallback(&PacketCollector::callback, &collector);

  ProcessResult r = proc.process(apex::compat::bytes_span{header.data(), header.size()});

  // Due to resync, some idle packets may be extracted from leftover bytes.
  // The key point is that resync drops occurred.
  EXPECT_GT(r.resyncDrops, 0U);
}

/* ----------------------------- Counter Tests ------------------------------- */

/** @test Verify counters track statistics correctly. */
TEST(EppProcessorTest, CountersTracking) {
  std::vector<std::uint8_t> payload = {0xAA, 0xBB};
  const std::uint8_t TOTAL_LEN = static_cast<std::uint8_t>(2 + payload.size());
  std::vector<std::uint8_t> header = buildEppHeader2(7, 1, TOTAL_LEN);
  std::vector<std::uint8_t> packet = createEppPacket(header, payload);

  ProcessorDefault proc;
  (void)proc.process(apex::compat::bytes_span{packet.data(), packet.size()});
  (void)proc.process(apex::compat::bytes_span{packet.data(), packet.size()});

  auto counters = proc.counters();
  EXPECT_EQ(counters.totalBytesIn, 8U); // 4 bytes x 2
  EXPECT_EQ(counters.totalPacketsExtracted, 2U);
  EXPECT_EQ(counters.totalCalls, 2U);

  proc.resetCounters();
  counters = proc.counters();
  EXPECT_EQ(counters.totalBytesIn, 0U);
  EXPECT_EQ(counters.totalPacketsExtracted, 0U);
}

/* ----------------------------- Config Tests -------------------------------- */

/** @test Verify config affects behavior. */
TEST(EppProcessorTest, MaxPacketLengthConfig) {
  // Create packet with length 100.
  std::vector<std::uint8_t> payload(96, 0x55); // 96 + 4 = 100
  std::vector<std::uint8_t> header = buildEppHeader4(7, 1, 0, 0, 100);
  std::vector<std::uint8_t> packet = createEppPacket(header, payload);

  ProcessorDefault proc;
  ProcessorConfig cfg;
  cfg.maxPacketLength = 50; // Less than packet size.
  proc.setConfig(cfg);

  ProcessResult r = proc.process(apex::compat::bytes_span{packet.data(), packet.size()});

  // Should trigger resync due to length over max.
  EXPECT_GT(r.resyncDrops, 0U);
}

/* ----------------------------- ToString Tests ------------------------------ */

/** @test Verify toString for Status enum. */
TEST(EppProcessorTest, StatusToString) {
  EXPECT_STREQ(toString(Status::OK), "OK");
  EXPECT_STREQ(toString(Status::NEED_MORE), "NEED_MORE");
  EXPECT_STREQ(toString(Status::WARNING_DESYNC_DROPPED), "WARNING_DESYNC_DROPPED");
  EXPECT_STREQ(toString(Status::ERROR_LENGTH_OVER_MAX), "ERROR_LENGTH_OVER_MAX");
  EXPECT_STREQ(toString(Status::ERROR_BUFFER_FULL), "ERROR_BUFFER_FULL");
}

/* ----------------------------- No Callback Test ---------------------------- */

/** @test Verify processor works without callback (packets counted but not delivered). */
TEST(EppProcessorTest, NoCallbackStillCounts) {
  std::vector<std::uint8_t> packet = buildEppHeader1(7, 0);

  ProcessorDefault proc;
  // No callback set.

  ProcessResult r = proc.process(apex::compat::bytes_span{packet.data(), packet.size()});

  EXPECT_EQ(r.status, Status::OK);
  EXPECT_EQ(r.packetsExtracted, 1U);
  EXPECT_EQ(proc.counters().totalPacketsExtracted, 1U);
}

/* ----------------------------- Buffer Full Test ---------------------------- */

/** @test Verify buffer full status when capacity exceeded. */
TEST(EppProcessorTest, BufferFullStatus) {
  // Use small processor.
  Processor<64> proc;

  // Try to add more data than capacity.
  std::vector<std::uint8_t> data(100, 0xE0); // All idle packet bytes.

  ProcessResult r = proc.process(apex::compat::bytes_span{data.data(), data.size()});

  // Should have consumed only up to capacity and signaled buffer full.
  EXPECT_EQ(r.status, Status::ERROR_BUFFER_FULL);
  EXPECT_LT(r.bytesConsumed, data.size());
}
