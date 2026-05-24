/**
 * @file CcsdsEpp_pTest.cpp
 * @brief Performance tests for CCSDS Encapsulation Packet Protocol (EPP).
 *
 * Measures:
 *  - packPacket() throughput at 8B / 64B / 1KB payloads (4-octet header)
 *  - PacketViewer::create() and peekProtocolId() viewing throughput
 *  - Streaming processor single-packet and burst extraction
 *  - Counters access overhead
 *
 * Usage:
 *   ./CcsdsEpp_PTEST --csv results.csv
 *   ./CcsdsEpp_PTEST --quick
 *   ./CcsdsEpp_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "CcsdsEppCommonDefs.hpp"
#include "CcsdsEppMessagePacker.hpp"
#include "CcsdsEppMutableMessage.hpp"
#include "CcsdsEppProcessor.hpp"
#include "CcsdsEppViewer.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ub = vernier::bench;
namespace epp = protocols::ccsds::epp;

/* ----------------------------- Local Helpers ----------------------------- */

namespace {

inline const ub::PerfConfig& getCfg() { return ub::detail::getPerfConfig(); }

/**
 * @brief Build a valid EPP 4-octet-header packet into a buffer.
 */
std::size_t buildTestPacket4Octet(std::size_t payloadSize, std::uint8_t* out, std::size_t outCap) {
  std::vector<std::uint8_t> payload(payloadSize, 0xAA);
  std::size_t written = 0;
  bool ok = epp::packPacket(epp::EPP_HEADER_4_OCTET, 1, 0, 0, 0,
                            apex::compat::bytes_span{payload.data(), payload.size()}, out, outCap,
                            written);
  if (!ok) {
    return 0;
  }
  return written;
}

/**
 * @brief Build a stream of concatenated EPP packets.
 */
std::vector<std::uint8_t> buildPacketStream(std::size_t packetCount, std::size_t payloadSize) {
  const std::size_t PACKET_SIZE = epp::EPP_HEADER_4_OCTET + payloadSize;
  std::vector<std::uint8_t> stream(packetCount * PACKET_SIZE);

  std::array<std::uint8_t, 4096> buf{};
  for (std::size_t i = 0; i < packetCount; ++i) {
    std::size_t written = buildTestPacket4Octet(payloadSize, buf.data(), buf.size());
    std::memcpy(stream.data() + i * PACKET_SIZE, buf.data(), written);
  }
  return stream;
}

} // namespace

/* ----------------------------- Pack Throughput ----------------------------- */

/**
 * @brief packPacket() throughput on 8B payload (12B total).
 */
PERF_TEST(EppPack, Small8B) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  std::array<std::uint8_t, 64> outBuf{};
  std::array<std::uint8_t, 8> payload{};
  std::memset(payload.data(), 0xAA, payload.size());
  const apex::compat::bytes_span PAYLOAD_SPAN{payload.data(), payload.size()};

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      std::size_t written = 0;
      (void)epp::packPacket(epp::EPP_HEADER_4_OCTET, 1, 0, 0, 0, PAYLOAD_SPAN, outBuf.data(),
                            outBuf.size(), written);
    }
  });

  volatile std::size_t sink = 0;
  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        bool ok = epp::packPacket(epp::EPP_HEADER_4_OCTET, 1, 0, 0, 0, PAYLOAD_SPAN, outBuf.data(),
                                  outBuf.size(), written);
        ASSERT_TRUE(ok);
        sink = written;
      },
      "pack-8b");

  (void)sink;
  std::printf("\npackPacket (8B, 4-octet hdr): %.3f us (%.0f ops/s)\n", R.stats.median,
              R.callsPerSecond);
}

/**
 * @brief packPacket() throughput on 64B payload (68B total).
 */
PERF_TEST(EppPack, Medium64B) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  std::array<std::uint8_t, 128> outBuf{};
  std::array<std::uint8_t, 64> payload{};
  std::memset(payload.data(), 0xBB, payload.size());
  const apex::compat::bytes_span PAYLOAD_SPAN{payload.data(), payload.size()};

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      std::size_t written = 0;
      (void)epp::packPacket(epp::EPP_HEADER_4_OCTET, 1, 0, 0, 0, PAYLOAD_SPAN, outBuf.data(),
                            outBuf.size(), written);
    }
  });

  volatile std::size_t sink = 0;
  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        bool ok = epp::packPacket(epp::EPP_HEADER_4_OCTET, 1, 0, 0, 0, PAYLOAD_SPAN, outBuf.data(),
                                  outBuf.size(), written);
        ASSERT_TRUE(ok);
        sink = written;
      },
      "pack-64b");

  (void)sink;
  std::printf("\npackPacket (64B, 4-octet hdr): %.3f us (%.0f ops/s)\n", R.stats.median,
              R.callsPerSecond);
}

/**
 * @brief packPacket() throughput on 1KB payload (1028B total).
 */
PERF_TEST(EppPack, Large1KB) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  std::array<std::uint8_t, 2048> outBuf{};
  std::array<std::uint8_t, 1024> payload{};
  std::memset(payload.data(), 0xCC, payload.size());
  const apex::compat::bytes_span PAYLOAD_SPAN{payload.data(), payload.size()};

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      std::size_t written = 0;
      (void)epp::packPacket(epp::EPP_HEADER_4_OCTET, 1, 0, 0, 0, PAYLOAD_SPAN, outBuf.data(),
                            outBuf.size(), written);
    }
  });

  volatile std::size_t sink = 0;
  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        bool ok = epp::packPacket(epp::EPP_HEADER_4_OCTET, 1, 0, 0, 0, PAYLOAD_SPAN, outBuf.data(),
                                  outBuf.size(), written);
        ASSERT_TRUE(ok);
        sink = written;
      },
      "pack-1kb");

  (void)sink;
  std::printf("\npackPacket (1KB, 4-octet hdr): %.3f us (%.0f ops/s)\n", R.stats.median,
              R.callsPerSecond);
}

/* ----------------------------- Viewer Throughput ----------------------------- */

/**
 * @brief PacketViewer::create() throughput on 64B-payload packets.
 */
PERF_TEST(EppView, Create64B) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  std::array<std::uint8_t, 128> pktBuf{};
  const std::size_t PKT_LEN = buildTestPacket4Octet(64, pktBuf.data(), pktBuf.size());
  ASSERT_GT(PKT_LEN, 0u);
  const apex::compat::bytes_span PKT_SPAN{pktBuf.data(), PKT_LEN};

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      auto v = epp::PacketViewer::create(PKT_SPAN);
      (void)v;
    }
  });

  volatile std::uint8_t sink = 0;
  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        auto v = epp::PacketViewer::create(PKT_SPAN);
        ASSERT_TRUE(v.has_value());
        sink = v->hdr.protocolId();
      },
      "viewer-create-64b");

  (void)sink;
  std::printf("\nPacketViewer::create (64B): %.3f us (%.0f ops/s)\n", R.stats.median,
              R.callsPerSecond);
}

/**
 * @brief peekProtocolId() fast-path throughput.
 */
PERF_TEST(EppView, PeekProtocolId) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  std::array<std::uint8_t, 128> pktBuf{};
  const std::size_t PKT_LEN = buildTestPacket4Octet(64, pktBuf.data(), pktBuf.size());
  ASSERT_GT(PKT_LEN, 0u);
  const apex::compat::bytes_span PKT_SPAN{pktBuf.data(), PKT_LEN};

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      auto pid = epp::PacketViewer::peekProtocolId(PKT_SPAN);
      (void)pid;
    }
  });

  volatile std::uint8_t sink = 0;
  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        auto pid = epp::PacketViewer::peekProtocolId(PKT_SPAN);
        ASSERT_TRUE(pid.has_value());
        sink = *pid;
      },
      "peek-protocol-id");

  (void)sink;
  std::printf("\npeekProtocolId: %.3f us (%.0f ops/s)\n", R.stats.median, R.callsPerSecond);
}

/* ----------------------------- Processor Throughput ----------------------------- */

/**
 * @brief Processor::process() throughput on a single 64B-payload packet.
 */
PERF_TEST(EppProcessor, Single64B) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  std::array<std::uint8_t, 128> pktBuf{};
  const std::size_t PKT_LEN = buildTestPacket4Octet(64, pktBuf.data(), pktBuf.size());
  ASSERT_GT(PKT_LEN, 0u);
  const apex::compat::bytes_span PKT_SPAN{pktBuf.data(), PKT_LEN};

  std::size_t callbackCount = 0;
  auto callbackFn = [](void* ctx, apex::compat::bytes_span) noexcept {
    auto* count = static_cast<std::size_t*>(ctx);
    ++(*count);
  };

  epp::ProcessorDefault proc;
  proc.setPacketCallback(epp::PacketDelegate{callbackFn, &callbackCount});

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      proc.reset();
      callbackCount = 0;
      (void)proc.process(PKT_SPAN);
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        proc.reset();
        callbackCount = 0;
        auto r = proc.process(PKT_SPAN);
        ASSERT_EQ(r.status, epp::Status::OK);
        ASSERT_EQ(r.packetsExtracted, 1u);
      },
      "process-single-64b");

  std::printf("\nProcessor::process (1x64B): %.3f us (%.0f ops/s)\n", R.stats.median,
              R.callsPerSecond);
}

/**
 * @brief Processor::process() throughput on a burst of 10 concatenated 64B packets.
 */
PERF_TEST(EppProcessor, Burst10x64B) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  const auto STREAM = buildPacketStream(10, 64);
  const apex::compat::bytes_span STREAM_SPAN{STREAM.data(), STREAM.size()};

  std::size_t callbackCount = 0;
  auto callbackFn = [](void* ctx, apex::compat::bytes_span) noexcept {
    auto* count = static_cast<std::size_t*>(ctx);
    ++(*count);
  };

  epp::ProcessorDefault proc;
  proc.setPacketCallback(epp::PacketDelegate{callbackFn, &callbackCount});

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      proc.reset();
      callbackCount = 0;
      (void)proc.process(STREAM_SPAN);
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        proc.reset();
        callbackCount = 0;
        auto r = proc.process(STREAM_SPAN);
        ASSERT_EQ(r.status, epp::Status::OK);
        ASSERT_EQ(r.packetsExtracted, 10u);
      },
      "process-burst-10x64b");

  std::printf("\nProcessor::process (10x64B): %.3f us (%.0f ops/s, %.0f pkts/s)\n", R.stats.median,
              R.callsPerSecond, R.callsPerSecond * 10.0);
}

/* ----------------------------- Overhead ----------------------------- */

/**
 * @brief counters() access overhead.
 */
PERF_TEST(EppOverhead, Counters) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  epp::ProcessorDefault proc;

  std::array<std::uint8_t, 128> pktBuf{};
  const std::size_t PKT_LEN = buildTestPacket4Octet(8, pktBuf.data(), pktBuf.size());
  (void)proc.process(apex::compat::bytes_span{pktBuf.data(), PKT_LEN});

  volatile std::size_t sink = 0;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      auto c = proc.counters();
      sink = c.totalPacketsExtracted;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        auto c = proc.counters();
        sink = c.totalPacketsExtracted;
      },
      "counters-access");

  (void)sink;
  std::printf("\ncounters() access: %.3f us (%.0f ops/s)\n", R.stats.median, R.callsPerSecond);
}

PERF_MAIN()
