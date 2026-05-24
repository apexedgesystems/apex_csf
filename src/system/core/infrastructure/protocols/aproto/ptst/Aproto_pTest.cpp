/**
 * @file Aproto_pTest.cpp
 * @brief Performance tests for the APROTO protocol codec.
 *
 * Measures:
 *  - Header build / encode / decode throughput
 *  - Full packet encode with and without CRC32
 *  - Packet validation throughput
 *  - CRC32 throughput at 64B / 1KB / 64KB payloads
 *  - ACK/NAK encode latency
 *  - Round-trip encode + validate + view
 *
 * Usage:
 *   ./Aproto_PTEST --csv results.csv
 *   ./Aproto_PTEST --quick
 *   ./Aproto_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoCodec.hpp"
#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoStatus.hpp"
#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoTypes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ub = vernier::bench;

using namespace system_core::protocols::aproto;

/* ----------------------------- Header Operations ----------------------------- */

/**
 * @brief Header struct build throughput (no encoding).
 */
PERF_TEST(AprotoPerf, BuildHeader) {
  UB_PERF_GUARD(perf);

  std::uint16_t seq = 0;

  auto fn = [&]() {
    auto hdr = buildHeader(0x006500, 0x0001, seq++, 32, false, true, true);
    (void)hdr;
  };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "BuildHeader");
}

/**
 * @brief Header-only encode throughput.
 */
PERF_TEST(AprotoPerf, EncodeHeaderOnly) {
  UB_PERF_GUARD(perf);

  std::array<std::uint8_t, 32> buf{};
  AprotoHeader hdr = buildHeader(0x006500, 0x0001, 1, 0);

  auto fn = [&]() { (void)encodeHeader(hdr, {buf.data(), buf.size()}); };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "EncodeHeaderOnly");
}

/**
 * @brief Header-only decode throughput.
 */
PERF_TEST(AprotoPerf, DecodeHeaderOnly) {
  UB_PERF_GUARD(perf);

  std::array<std::uint8_t, 32> buf{};
  AprotoHeader hdr = buildHeader(0x006500, 0x0001, 42, 0);
  (void)encodeHeader(hdr, {buf.data(), buf.size()});

  AprotoHeader out{};

  auto fn = [&]() { (void)decodeHeader({buf.data(), APROTO_HEADER_SIZE}, out); };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "DecodeHeaderOnly");
}

/* ----------------------------- Full Packet Operations ----------------------------- */

/**
 * @brief Full packet encode throughput, no CRC.
 */
PERF_TEST(AprotoPerf, EncodePacketNoCrc) {
  UB_PERF_GUARD(perf);

  std::array<std::uint8_t, 128> buf{};
  std::array<std::uint8_t, 32> payload{};
  std::fill(payload.begin(), payload.end(), 0xAB);

  AprotoHeader hdr = buildHeader(0x006500, 0x0001, 1, 32, false, false, false);

  auto fn = [&]() {
    std::size_t written = 0;
    (void)encodePacket(hdr, {payload.data(), payload.size()}, {buf.data(), buf.size()}, written);
  };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "EncodePacket_32B_NoCrc");
}

/**
 * @brief Full packet encode throughput with CRC32.
 */
PERF_TEST(AprotoPerf, EncodePacketWithCrc) {
  UB_PERF_GUARD(perf);

  std::array<std::uint8_t, 128> buf{};
  std::array<std::uint8_t, 32> payload{};
  std::fill(payload.begin(), payload.end(), 0xAB);

  AprotoHeader hdr = buildHeader(0x006500, 0x0001, 1, 32, false, false, true);

  auto fn = [&]() {
    std::size_t written = 0;
    (void)encodePacket(hdr, {payload.data(), payload.size()}, {buf.data(), buf.size()}, written);
  };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "EncodePacket_32B_WithCrc");
}

/**
 * @brief Packet validate throughput (includes CRC check).
 */
PERF_TEST(AprotoPerf, ValidatePacketWithCrc) {
  UB_PERF_GUARD(perf);

  std::array<std::uint8_t, 128> buf{};
  std::array<std::uint8_t, 32> payload{};
  std::fill(payload.begin(), payload.end(), 0xAB);

  AprotoHeader hdr = buildHeader(0x006500, 0x0001, 1, 32, false, false, true);
  std::size_t written = 0;
  (void)encodePacket(hdr, {payload.data(), payload.size()}, {buf.data(), buf.size()}, written);

  auto fn = [&]() { (void)validatePacket({buf.data(), written}); };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "ValidatePacket_32B_Crc");
}

/**
 * @brief PacketView creation throughput.
 */
PERF_TEST(AprotoPerf, CreatePacketView) {
  UB_PERF_GUARD(perf);

  std::array<std::uint8_t, 128> buf{};
  std::array<std::uint8_t, 32> payload{};
  std::fill(payload.begin(), payload.end(), 0xAB);

  AprotoHeader hdr = buildHeader(0x006500, 0x0001, 1, 32, false, false, true);
  std::size_t written = 0;
  (void)encodePacket(hdr, {payload.data(), payload.size()}, {buf.data(), buf.size()}, written);

  auto fn = [&]() {
    PacketView view{};
    (void)createPacketView({buf.data(), written}, view);
  };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "CreatePacketView_32B");
}

/* ----------------------------- CRC Throughput ----------------------------- */

/**
 * @brief CRC32 throughput on a 64B buffer.
 */
PERF_TEST(AprotoPerf, ComputeCrc_64B) {
  UB_PERF_GUARD(perf);

  std::array<std::uint8_t, 64> data{};
  std::fill(data.begin(), data.end(), 0xCD);

  auto fn = [&]() {
    volatile auto crc = computeCrc({data.data(), data.size()});
    (void)crc;
  };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "ComputeCrc_64B");
}

/**
 * @brief CRC32 throughput on a 1KB buffer.
 */
PERF_TEST(AprotoPerf, ComputeCrc_1KB) {
  UB_PERF_GUARD(perf);

  std::vector<std::uint8_t> data(1024, 0xEF);

  auto fn = [&]() {
    volatile auto crc = computeCrc({data.data(), data.size()});
    (void)crc;
  };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "ComputeCrc_1KB");
}

/**
 * @brief CRC32 throughput on a 64KB buffer.
 */
PERF_TEST(AprotoPerf, ComputeCrc_64KB) {
  UB_PERF_GUARD(perf);

  std::vector<std::uint8_t> data(65536, 0x12);

  auto fn = [&]() {
    volatile auto crc = computeCrc({data.data(), data.size()});
    (void)crc;
  };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "ComputeCrc_64KB");
}

/* ----------------------------- ACK / NAK ----------------------------- */

/**
 * @brief ACK response encode throughput, no CRC.
 */
PERF_TEST(AprotoPerf, EncodeAck) {
  UB_PERF_GUARD(perf);

  std::array<std::uint8_t, 64> buf{};
  AprotoHeader cmdHdr = buildHeader(0x006500, 0x0100, 42, 8);

  auto fn = [&]() {
    std::size_t written = 0;
    (void)encodeAckNak(cmdHdr, 0, {buf.data(), buf.size()}, written, false);
  };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "EncodeAck_NoCrc");
}

/**
 * @brief NAK response encode throughput with CRC.
 */
PERF_TEST(AprotoPerf, EncodeNakWithCrc) {
  UB_PERF_GUARD(perf);

  std::array<std::uint8_t, 64> buf{};
  AprotoHeader cmdHdr = buildHeader(0x006500, 0x0100, 42, 8);

  auto fn = [&]() {
    std::size_t written = 0;
    (void)encodeAckNak(cmdHdr, 0x01, {buf.data(), buf.size()}, written, true);
  };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "EncodeNak_WithCrc");
}

/* ----------------------------- Round Trip ----------------------------- */

/**
 * @brief Full encode + validate + view round-trip throughput with CRC.
 */
PERF_TEST(AprotoPerf, RoundTripWithCrc) {
  UB_PERF_GUARD(perf);

  std::array<std::uint8_t, 128> buf{};
  std::array<std::uint8_t, 16> payload{};
  std::fill(payload.begin(), payload.end(), 0xDE);

  auto fn = [&]() {
    AprotoHeader hdr = buildHeader(0x006500, 0x0001, 1, 16, false, true, true);
    std::size_t written = 0;
    (void)encodePacket(hdr, {payload.data(), payload.size()}, {buf.data(), buf.size()}, written);

    (void)validatePacket({buf.data(), written});
    PacketView view{};
    (void)createPacketView({buf.data(), written}, view);
  };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "RoundTrip_16B_Crc");
}

/* ----------------------------- Payload Extraction ----------------------------- */

/**
 * @brief Payload span extraction throughput.
 */
PERF_TEST(AprotoPerf, GetPayload) {
  UB_PERF_GUARD(perf);

  std::array<std::uint8_t, 128> buf{};
  std::array<std::uint8_t, 32> payload{};
  std::fill(payload.begin(), payload.end(), 0xAB);

  AprotoHeader hdr = buildHeader(0x006500, 0x0001, 1, 32, false, false, false);
  std::size_t written = 0;
  (void)encodePacket(hdr, {payload.data(), payload.size()}, {buf.data(), buf.size()}, written);

  auto fn = [&]() {
    auto payloadSpan = getPayload({buf.data(), written});
    (void)payloadSpan.size();
  };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "GetPayload_32B");
}

/* ----------------------------- Status Conversion ----------------------------- */

/**
 * @brief Status enum to C-string conversion throughput.
 */
PERF_TEST(AprotoPerf, StatusToString) {
  UB_PERF_GUARD(perf);

  std::array<Status, 5> statuses = {Status::SUCCESS, Status::ERROR_BUFFER_TOO_SMALL,
                                    Status::ERROR_INVALID_MAGIC, Status::ERROR_CRC_MISMATCH,
                                    Status::ERROR_INVALID_VERSION};
  std::size_t idx = 0;

  auto fn = [&]() {
    const char* str = toString(statuses[idx++ % statuses.size()]);
    (void)str;
  };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "StatusToString");
}

PERF_MAIN()
