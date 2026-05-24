/**
 * @file COBSFraming_pTest.cpp
 * @brief Performance tests for COBS encode/decode framing.
 *
 * Measures:
 *  - Encode throughput on clean / realistic / worst-case payloads (256B)
 *  - Decode throughput on single-chunk and streaming (chunked) inputs
 *  - Payload-size scaling sweep (64B to 256KB)
 *  - Cache hierarchy probes (L1 / L3 / RAM)
 *  - COBS block-boundary behavior at 254 and 255 bytes
 *  - Encode-vs-decode balance at 1KB
 *
 * Usage:
 *   ./COBSFraming_PTEST --csv results.csv
 *   ./COBSFraming_PTEST --quick
 *   ./COBSFraming_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/infrastructure/protocols/framing/cobs/inc/COBSFraming.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace ub = vernier::bench;
namespace cobs = apex::protocols::cobs;

/* ----------------------------- Test Data Generators ----------------------------- */

/**
 * @brief Generate a payload of @p size bytes with the requested zero-byte density.
 */
static std::vector<std::uint8_t> generatePayload(std::size_t size, double zeroDensity = 0.0) {
  std::vector<std::uint8_t> payload(size);
  std::mt19937 rng(42);
  std::uniform_real_distribution<> zeroProb(0.0, 1.0);
  std::uniform_int_distribution<> byteDist(1, 255);

  for (auto& byte : payload) {
    if (zeroProb(rng) < zeroDensity) {
      byte = 0x00;
    } else {
      byte = static_cast<std::uint8_t>(byteDist(rng));
    }
  }
  return payload;
}

/** @brief Best-case COBS payload (no zero bytes). */
static std::vector<std::uint8_t> generateCleanPayload(std::size_t size) {
  return generatePayload(size, 0.0);
}

/** @brief Realistic binary-data payload (~5% zero bytes). */
static std::vector<std::uint8_t> generateRealisticPayload(std::size_t size) {
  return generatePayload(size, 0.05);
}

/** @brief Stress-test payload (~50% zero bytes). */
static std::vector<std::uint8_t> generateWorstCasePayload(std::size_t size) {
  return generatePayload(size, 0.5);
}

/* ----------------------------- Core Encode/Decode ----------------------------- */

/**
 * @brief Encode 256B clean payload (no zeros, best case).
 */
PERF_TEST(COBSFramingPerf, EncodeClean) {
  UB_PERF_GUARD(perf);

  constexpr std::size_t PAYLOAD_SIZE = 256;
  auto payload = generateCleanPayload(PAYLOAD_SIZE);
  std::vector<std::uint8_t> encoded(PAYLOAD_SIZE * 3);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      auto r = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                            encoded.data(), encoded.size());
      (void)r;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        auto r = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                              encoded.data(), encoded.size());
        ASSERT_EQ(r.status, cobs::Status::OK);
      },
      "encode_clean");

  std::printf("\n[EncodeClean] %zu bytes, no zeros\n", PAYLOAD_SIZE);
  std::printf("  Latency: %.3f us (median), %.3f us (p10), %.3f us (p90)\n", result.stats.median,
              result.stats.p10, result.stats.p90);
  std::printf("  Throughput: %.0f calls/sec, %.2f MB/s\n", result.callsPerSecond,
              (result.callsPerSecond * PAYLOAD_SIZE) / 1e6);
  std::printf("  Jitter (CV%%): %.1f%%\n", result.stats.cv * 100);
}

/**
 * @brief Encode 256B worst-case payload (~50% zeros).
 */
PERF_TEST(COBSFramingPerf, EncodeWorstCase) {
  UB_PERF_GUARD(perf);

  constexpr std::size_t PAYLOAD_SIZE = 256;
  auto payload = generateWorstCasePayload(PAYLOAD_SIZE);
  std::vector<std::uint8_t> encoded(PAYLOAD_SIZE * 3);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      auto r = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                            encoded.data(), encoded.size());
      (void)r;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        auto r = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                              encoded.data(), encoded.size());
        ASSERT_EQ(r.status, cobs::Status::OK);
      },
      "encode_worst");

  std::printf("\n[EncodeWorstCase] %zu bytes, ~50%% zeros\n", PAYLOAD_SIZE);
  std::printf("  Latency: %.3f us (median), %.3f us (p10), %.3f us (p90)\n", result.stats.median,
              result.stats.p10, result.stats.p90);
  std::printf("  Throughput: %.0f calls/sec, %.2f MB/s\n", result.callsPerSecond,
              (result.callsPerSecond * PAYLOAD_SIZE) / 1e6);
  std::printf("  Jitter (CV%%): %.1f%%\n", result.stats.cv * 100);
}

/**
 * @brief Encode 256B realistic payload (~5% zeros, typical binary data).
 */
PERF_TEST(COBSFramingPerf, EncodeRealistic) {
  UB_PERF_GUARD(perf);

  constexpr std::size_t PAYLOAD_SIZE = 256;
  auto payload = generateRealisticPayload(PAYLOAD_SIZE);
  std::vector<std::uint8_t> encoded(PAYLOAD_SIZE * 3);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      auto r = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                            encoded.data(), encoded.size());
      (void)r;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        auto r = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                              encoded.data(), encoded.size());
        ASSERT_EQ(r.status, cobs::Status::OK);
      },
      "encode_realistic");

  std::printf("\n[EncodeRealistic] %zu bytes, ~5%% zeros\n", PAYLOAD_SIZE);
  std::printf("  Latency: %.3f us (median), %.3f us (p10), %.3f us (p90)\n", result.stats.median,
              result.stats.p10, result.stats.p90);
  std::printf("  Throughput: %.0f calls/sec, %.2f MB/s\n", result.callsPerSecond,
              (result.callsPerSecond * PAYLOAD_SIZE) / 1e6);
  std::printf("  Jitter (CV%%): %.1f%%\n", result.stats.cv * 100);
}

/**
 * @brief Decode 256B clean payload (no original zeros).
 */
PERF_TEST(COBSFramingPerf, DecodeClean) {
  UB_PERF_GUARD(perf);

  constexpr std::size_t PAYLOAD_SIZE = 256;
  auto payload = generateCleanPayload(PAYLOAD_SIZE);

  std::vector<std::uint8_t> encoded(PAYLOAD_SIZE * 3);
  auto encResult = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                                encoded.data(), encoded.size());
  ASSERT_EQ(encResult.status, cobs::Status::OK);
  encoded.resize(encResult.bytesProduced);

  std::vector<std::uint8_t> decoded(PAYLOAD_SIZE);
  cobs::DecodeState state;
  cobs::DecodeConfig config;
  config.maxFrameSize = PAYLOAD_SIZE;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      state.reset();
      auto r =
          cobs::decodeChunk(state, config, apex::compat::bytes_span{encoded.data(), encoded.size()},
                            decoded.data(), decoded.size());
      (void)r;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        state.reset();
        auto r = cobs::decodeChunk(state, config,
                                   apex::compat::bytes_span{encoded.data(), encoded.size()},
                                   decoded.data(), decoded.size());
        ASSERT_EQ(r.status, cobs::Status::OK);
        ASSERT_TRUE(r.frameCompleted);
      },
      "decode_clean");

  std::printf("\n[DecodeClean] %zu bytes, no original zeros\n", PAYLOAD_SIZE);
  std::printf("  Latency: %.3f us (median), %.3f us (p10), %.3f us (p90)\n", result.stats.median,
              result.stats.p10, result.stats.p90);
  std::printf("  Throughput: %.0f calls/sec, %.2f MB/s\n", result.callsPerSecond,
              (result.callsPerSecond * PAYLOAD_SIZE) / 1e6);
  std::printf("  Jitter (CV%%): %.1f%%\n", result.stats.cv * 100);
}

/**
 * @brief Decode 256B worst-case payload (~50% original zeros).
 */
PERF_TEST(COBSFramingPerf, DecodeWorstCase) {
  UB_PERF_GUARD(perf);

  constexpr std::size_t PAYLOAD_SIZE = 256;
  auto payload = generateWorstCasePayload(PAYLOAD_SIZE);

  std::vector<std::uint8_t> encoded(PAYLOAD_SIZE * 3);
  auto encResult = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                                encoded.data(), encoded.size());
  ASSERT_EQ(encResult.status, cobs::Status::OK);
  encoded.resize(encResult.bytesProduced);

  std::vector<std::uint8_t> decoded(PAYLOAD_SIZE);
  cobs::DecodeState state;
  cobs::DecodeConfig config;
  config.maxFrameSize = PAYLOAD_SIZE;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      state.reset();
      auto r =
          cobs::decodeChunk(state, config, apex::compat::bytes_span{encoded.data(), encoded.size()},
                            decoded.data(), decoded.size());
      (void)r;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        state.reset();
        auto r = cobs::decodeChunk(state, config,
                                   apex::compat::bytes_span{encoded.data(), encoded.size()},
                                   decoded.data(), decoded.size());
        ASSERT_EQ(r.status, cobs::Status::OK);
        ASSERT_TRUE(r.frameCompleted);
      },
      "decode_worst");

  std::printf("\n[DecodeWorstCase] %zu bytes, ~50%% original zeros\n", PAYLOAD_SIZE);
  std::printf("  Latency: %.3f us (median), %.3f us (p10), %.3f us (p90)\n", result.stats.median,
              result.stats.p10, result.stats.p90);
  std::printf("  Throughput: %.0f calls/sec, %.2f MB/s\n", result.callsPerSecond,
              (result.callsPerSecond * PAYLOAD_SIZE) / 1e6);
  std::printf("  Jitter (CV%%): %.1f%%\n", result.stats.cv * 100);
}

/**
 * @brief Decode 256B realistic payload (~5% original zeros).
 */
PERF_TEST(COBSFramingPerf, DecodeRealistic) {
  UB_PERF_GUARD(perf);

  constexpr std::size_t PAYLOAD_SIZE = 256;
  auto payload = generateRealisticPayload(PAYLOAD_SIZE);

  std::vector<std::uint8_t> encoded(PAYLOAD_SIZE * 3);
  auto encResult = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                                encoded.data(), encoded.size());
  ASSERT_EQ(encResult.status, cobs::Status::OK);
  encoded.resize(encResult.bytesProduced);

  std::vector<std::uint8_t> decoded(PAYLOAD_SIZE);
  cobs::DecodeState state;
  cobs::DecodeConfig config;
  config.maxFrameSize = PAYLOAD_SIZE;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      state.reset();
      auto r =
          cobs::decodeChunk(state, config, apex::compat::bytes_span{encoded.data(), encoded.size()},
                            decoded.data(), decoded.size());
      (void)r;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        state.reset();
        auto r = cobs::decodeChunk(state, config,
                                   apex::compat::bytes_span{encoded.data(), encoded.size()},
                                   decoded.data(), decoded.size());
        ASSERT_EQ(r.status, cobs::Status::OK);
        ASSERT_TRUE(r.frameCompleted);
      },
      "decode_realistic");

  std::printf("\n[DecodeRealistic] %zu bytes, ~5%% original zeros\n", PAYLOAD_SIZE);
  std::printf("  Latency: %.3f us (median), %.3f us (p10), %.3f us (p90)\n", result.stats.median,
              result.stats.p10, result.stats.p90);
  std::printf("  Throughput: %.0f calls/sec, %.2f MB/s\n", result.callsPerSecond,
              (result.callsPerSecond * PAYLOAD_SIZE) / 1e6);
  std::printf("  Jitter (CV%%): %.1f%%\n", result.stats.cv * 100);
}

/**
 * @brief Decode 256B realistic payload via 8-byte streaming chunks.
 */
PERF_TEST(COBSFramingPerf, DecodeStreaming) {
  UB_PERF_GUARD(perf);

  constexpr std::size_t PAYLOAD_SIZE = 256;
  auto payload = generateRealisticPayload(PAYLOAD_SIZE);

  std::vector<std::uint8_t> encoded(PAYLOAD_SIZE * 3);
  auto encResult = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                                encoded.data(), encoded.size());
  ASSERT_EQ(encResult.status, cobs::Status::OK);
  encoded.resize(encResult.bytesProduced);

  constexpr std::size_t CHUNK_SIZE = 8;
  cobs::DecodeConfig config;
  config.maxFrameSize = PAYLOAD_SIZE;
  std::vector<std::uint8_t> decoded(PAYLOAD_SIZE);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      cobs::DecodeState state;
      std::size_t inPos = 0;
      std::size_t outPos = 0;

      while (inPos < encoded.size()) {
        const std::size_t chunkLen = std::min(CHUNK_SIZE, encoded.size() - inPos);
        auto r = cobs::decodeChunk(state, config,
                                   apex::compat::bytes_span{encoded.data() + inPos, chunkLen},
                                   decoded.data() + outPos, decoded.size() - outPos);
        inPos += r.bytesConsumed;
        outPos += r.bytesProduced;
        if (r.frameCompleted)
          break;
      }
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        cobs::DecodeState state;
        std::size_t inPos = 0;
        std::size_t outPos = 0;

        while (inPos < encoded.size()) {
          const std::size_t chunkLen = std::min(CHUNK_SIZE, encoded.size() - inPos);
          auto r = cobs::decodeChunk(state, config,
                                     apex::compat::bytes_span{encoded.data() + inPos, chunkLen},
                                     decoded.data() + outPos, decoded.size() - outPos);
          inPos += r.bytesConsumed;
          outPos += r.bytesProduced;
          if (r.frameCompleted)
            break;
        }

        if (outPos != PAYLOAD_SIZE) {
          std::fprintf(stderr, "Streaming decode incomplete: got %zu, expected %zu\n", outPos,
                       PAYLOAD_SIZE);
        }
      },
      "decode_streaming");

  std::printf("\n[DecodeStreaming] %zu bytes in %zu-byte chunks\n", PAYLOAD_SIZE, CHUNK_SIZE);
  std::printf("  Latency: %.3f us (median), %.3f us (p10), %.3f us (p90)\n", result.stats.median,
              result.stats.p10, result.stats.p90);
  std::printf("  Throughput: %.0f calls/sec, %.2f MB/s\n", result.callsPerSecond,
              (result.callsPerSecond * PAYLOAD_SIZE) / 1e6);
  std::printf("  Jitter (CV%%): %.1f%%\n", result.stats.cv * 100);
}

/* ----------------------------- Payload Sweep ----------------------------- */

/**
 * @brief Parameterized encode/decode sweep across payload sizes (64B to 256KB).
 */
class COBSPayloadSweep : public ::testing::TestWithParam<std::size_t> {};

TEST_P(COBSPayloadSweep, Encode) {
  const std::size_t PAYLOAD_SIZE = GetParam();

  std::string testName = "COBSPayloadSweep.Encode/" + std::to_string(PAYLOAD_SIZE);
  ub::PerfCase perf{testName, ub::detail::getPerfConfig()};

  auto payload = generateRealisticPayload(PAYLOAD_SIZE);
  std::vector<std::uint8_t> encoded(PAYLOAD_SIZE * 3);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      auto r = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                            encoded.data(), encoded.size());
      (void)r;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        auto r = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                              encoded.data(), encoded.size());
        ASSERT_EQ(r.status, cobs::Status::OK);
      },
      "encode");

  std::printf("\n[Encode] %zu bytes: %.3f us, %.0f calls/sec, %.2f MB/s, CV %.1f%%\n", PAYLOAD_SIZE,
              result.stats.median, result.callsPerSecond,
              (result.callsPerSecond * PAYLOAD_SIZE) / 1e6, result.stats.cv * 100);
}

TEST_P(COBSPayloadSweep, Decode) {
  const std::size_t PAYLOAD_SIZE = GetParam();

  std::string testName = "COBSPayloadSweep.Decode/" + std::to_string(PAYLOAD_SIZE);
  ub::PerfCase perf{testName, ub::detail::getPerfConfig()};

  auto payload = generateRealisticPayload(PAYLOAD_SIZE);
  std::vector<std::uint8_t> encoded(PAYLOAD_SIZE * 3);
  auto encResult = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                                encoded.data(), encoded.size());
  ASSERT_EQ(encResult.status, cobs::Status::OK);
  encoded.resize(encResult.bytesProduced);

  std::vector<std::uint8_t> decoded(PAYLOAD_SIZE);
  cobs::DecodeState state;
  cobs::DecodeConfig config;
  config.maxFrameSize = PAYLOAD_SIZE;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      state.reset();
      auto r =
          cobs::decodeChunk(state, config, apex::compat::bytes_span{encoded.data(), encoded.size()},
                            decoded.data(), decoded.size());
      (void)r;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        state.reset();
        auto r = cobs::decodeChunk(state, config,
                                   apex::compat::bytes_span{encoded.data(), encoded.size()},
                                   decoded.data(), decoded.size());
        ASSERT_EQ(r.status, cobs::Status::OK);
      },
      "decode");

  std::printf("\n[Decode] %zu bytes: %.3f us, %.0f calls/sec, %.2f MB/s, CV %.1f%%\n", PAYLOAD_SIZE,
              result.stats.median, result.callsPerSecond,
              (result.callsPerSecond * PAYLOAD_SIZE) / 1e6, result.stats.cv * 100);
}

INSTANTIATE_TEST_SUITE_P(PayloadSizes, COBSPayloadSweep,
                         ::testing::Values(64, 256, 1024, 4096, 16384, 65536, 262144));

/* ----------------------------- Cache Hierarchy ----------------------------- */

/**
 * @brief Encode with L1-resident payload (~16KB).
 */
PERF_TEST(COBSCache, EncodeCacheL1) {
  UB_PERF_GUARD(perf);

  constexpr std::size_t PAYLOAD_SIZE = 16 * 1024;
  auto payload = generateRealisticPayload(PAYLOAD_SIZE);
  std::vector<std::uint8_t> encoded(PAYLOAD_SIZE * 3);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      auto r = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                            encoded.data(), encoded.size());
      (void)r;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        auto r = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                              encoded.data(), encoded.size());
        ASSERT_EQ(r.status, cobs::Status::OK);
      },
      "encode_L1");

  std::printf("\n[EncodeL1] %zu KB\n", PAYLOAD_SIZE / 1024);
  std::printf("  Throughput: %.2f MB/s\n", (result.callsPerSecond * PAYLOAD_SIZE) / 1e6);
  std::printf("  Jitter (CV%%): %.1f%%\n", result.stats.cv * 100);
}

/**
 * @brief Encode with L3-resident payload (~1MB).
 */
PERF_TEST(COBSCache, EncodeCacheL3) {
  UB_PERF_GUARD(perf);

  constexpr std::size_t PAYLOAD_SIZE = 1024 * 1024;
  auto payload = generateRealisticPayload(PAYLOAD_SIZE);
  std::vector<std::uint8_t> encoded(PAYLOAD_SIZE * 3);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles() / 10; ++i) {
      auto r = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                            encoded.data(), encoded.size());
      (void)r;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        auto r = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                              encoded.data(), encoded.size());
        ASSERT_EQ(r.status, cobs::Status::OK);
      },
      "encode_L3");

  std::printf("\n[EncodeL3] %zu MB\n", PAYLOAD_SIZE / (1024 * 1024));
  std::printf("  Throughput: %.2f MB/s\n", (result.callsPerSecond * PAYLOAD_SIZE) / 1e6);
  std::printf("  Jitter (CV%%): %.1f%%\n", result.stats.cv * 100);
}

/**
 * @brief Encode with RAM-resident payload (4MB, exceeds typical cache).
 */
PERF_TEST(COBSCache, EncodeCacheRAM) {
  UB_PERF_GUARD(perf);

  constexpr std::size_t PAYLOAD_SIZE = 4 * 1024 * 1024;
  auto payload = generateRealisticPayload(PAYLOAD_SIZE);
  std::vector<std::uint8_t> encoded(PAYLOAD_SIZE * 3);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles() / 10; ++i) {
      auto r = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                            encoded.data(), encoded.size());
      (void)r;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        auto r = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                              encoded.data(), encoded.size());
        ASSERT_EQ(r.status, cobs::Status::OK);
      },
      "encode_RAM");

  std::printf("\n[EncodeRAM] %zu MB\n", PAYLOAD_SIZE / (1024 * 1024));
  std::printf("  Throughput: %.2f MB/s\n", (result.callsPerSecond * PAYLOAD_SIZE) / 1e6);
  std::printf("  Jitter (CV%%): %.1f%%\n", result.stats.cv * 100);
}

/* ----------------------------- Boundary ----------------------------- */

/**
 * @brief Encode exactly 254 bytes (one COBS block).
 */
PERF_TEST(COBSBoundary, Encode254Bytes) {
  UB_PERF_GUARD(perf);

  constexpr std::size_t PAYLOAD_SIZE = 254;
  auto payload = generateCleanPayload(PAYLOAD_SIZE);
  std::vector<std::uint8_t> encoded(PAYLOAD_SIZE * 3);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      auto r = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                            encoded.data(), encoded.size());
      (void)r;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        auto r = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                              encoded.data(), encoded.size());
        ASSERT_EQ(r.status, cobs::Status::OK);
      },
      "encode_254");

  std::printf("\n[Boundary254] Encoding exactly 254 bytes (one COBS block)\n");
  std::printf("  Latency: %.3f us\n", result.stats.median);
  std::printf("  Throughput: %.0f calls/sec\n", result.callsPerSecond);
}

/**
 * @brief Encode 255 bytes (forces two COBS blocks).
 */
PERF_TEST(COBSBoundary, Encode255Bytes) {
  UB_PERF_GUARD(perf);

  constexpr std::size_t PAYLOAD_SIZE = 255;
  auto payload = generateCleanPayload(PAYLOAD_SIZE);
  std::vector<std::uint8_t> encoded(PAYLOAD_SIZE * 3);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      auto r = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                            encoded.data(), encoded.size());
      (void)r;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        auto r = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                              encoded.data(), encoded.size());
        ASSERT_EQ(r.status, cobs::Status::OK);
      },
      "encode_255");

  std::printf("\n[Boundary255] Encoding 255 bytes (two COBS blocks)\n");
  std::printf("  Latency: %.3f us\n", result.stats.median);
  std::printf("  Throughput: %.0f calls/sec\n", result.callsPerSecond);
}

/* ----------------------------- Encode vs Decode ----------------------------- */

/**
 * @brief Side-by-side encode vs decode at 1KB.
 */
PERF_TEST(COBSComparison, EncodeVsDecode) {
  UB_PERF_GUARD(perf);

  constexpr std::size_t PAYLOAD_SIZE = 1024;
  auto payload = generateRealisticPayload(PAYLOAD_SIZE);
  std::vector<std::uint8_t> encoded(PAYLOAD_SIZE * 3);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      auto r = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                            encoded.data(), encoded.size());
      (void)r;
    }
  });

  auto encResult = perf.throughputLoop(
      [&] {
        auto r = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                              encoded.data(), encoded.size());
        ASSERT_EQ(r.status, cobs::Status::OK);
      },
      "encode");

  auto encSetup = cobs::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                               encoded.data(), encoded.size());
  ASSERT_EQ(encSetup.status, cobs::Status::OK);
  encoded.resize(encSetup.bytesProduced);

  std::vector<std::uint8_t> decoded(PAYLOAD_SIZE);
  cobs::DecodeState state;
  cobs::DecodeConfig config;
  config.maxFrameSize = PAYLOAD_SIZE;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      state.reset();
      auto r =
          cobs::decodeChunk(state, config, apex::compat::bytes_span{encoded.data(), encoded.size()},
                            decoded.data(), decoded.size());
      (void)r;
    }
  });

  auto decResult = perf.throughputLoop(
      [&] {
        state.reset();
        auto r = cobs::decodeChunk(state, config,
                                   apex::compat::bytes_span{encoded.data(), encoded.size()},
                                   decoded.data(), decoded.size());
        ASSERT_EQ(r.status, cobs::Status::OK);
      },
      "decode");

  std::printf("\n=== Encode vs Decode Comparison (%zu bytes) ===\n", PAYLOAD_SIZE);
  std::printf("Encode: %.3f us (%.0f calls/sec)\n", encResult.stats.median,
              encResult.callsPerSecond);
  std::printf("Decode: %.3f us (%.0f calls/sec)\n", decResult.stats.median,
              decResult.callsPerSecond);

  double ratio = encResult.stats.median / decResult.stats.median;
  std::printf("Encode/Decode ratio: %.2fx ", ratio);
  if (ratio > 1.0) {
    std::printf("(Encode slower)\n");
  } else {
    std::printf("(Decode slower)\n");
  }
}

PERF_MAIN()
