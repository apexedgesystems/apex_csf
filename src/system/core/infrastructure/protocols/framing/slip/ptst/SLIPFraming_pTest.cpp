/**
 * @file SLIPFraming_pTest.cpp
 * @brief Performance tests for SLIP framing encode/decode.
 *
 * Measures:
 *  - Encode throughput (clean, worst-case, realistic payloads)
 *  - Decode throughput (single-frame, streaming, multi-frame)
 *  - Payload size scaling (64 B - 256 KB)
 *  - Cache hierarchy effects (L1, L3, RAM)
 *  - Delimiter configuration overhead
 *  - Encode vs decode comparison
 *
 * Usage:
 *   ./SLIPFraming_PTEST --csv results.csv
 *   ./SLIPFraming_PTEST --quick
 *   ./SLIPFraming_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/infrastructure/protocols/framing/slip/inc/SLIPFraming.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ub = vernier::bench;
namespace slip = apex::protocols::slip;

/* ----------------------------- Local Helpers ----------------------------- */

namespace {

inline const ub::PerfConfig& getCfg() { return ub::detail::getPerfConfig(); }

inline std::vector<std::uint8_t> makeCleanPayload(std::size_t n) {
  std::vector<std::uint8_t> payload(n);
  for (std::size_t i = 0; i < n; ++i) {
    payload[i] = static_cast<std::uint8_t>(0x20 + (i % 80));
  }
  return payload;
}

inline std::vector<std::uint8_t> makeWorstCasePayload(std::size_t n) {
  std::vector<std::uint8_t> payload(n);
  for (std::size_t i = 0; i < n; ++i) {
    payload[i] = (i % 2 == 0) ? slip::END : slip::ESC;
  }
  return payload;
}

inline std::vector<std::uint8_t> makeRealisticPayload(std::size_t n) {
  std::vector<std::uint8_t> payload(n);
  for (std::size_t i = 0; i < n; ++i) {
    if (i % 20 == 0) {
      payload[i] = slip::END;
    } else {
      payload[i] = static_cast<std::uint8_t>(0x20 + (i % 80));
    }
  }
  return payload;
}

} // namespace

/* ----------------------------- Encode Tests ----------------------------- */

/**
 * @brief Encode throughput with clean payload (no escapes, best case).
 */
PERF_TEST(SLIPFramingPerf, EncodeClean) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  const std::size_t PAYLOAD_SIZE = static_cast<std::size_t>(getCfg().msgBytes);
  const auto payload = makeCleanPayload(PAYLOAD_SIZE);

  std::vector<std::uint8_t> encoded(payload.size() * 2 + 2);

  perf.setup([&] {
    auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()}, encoded.data(),
                          encoded.size());
    ASSERT_EQ(r.status, slip::Status::OK);
  });

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                                     encoded.data(), encoded.size());
      (void)r;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                              encoded.data(), encoded.size());
        ASSERT_EQ(r.status, slip::Status::OK);
      },
      "encode-clean");
}

/**
 * @brief Encode throughput with maximum escaping (all END/ESC bytes).
 */
PERF_TEST(SLIPFramingPerf, EncodeWorstCase) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  const std::size_t PAYLOAD_SIZE = static_cast<std::size_t>(getCfg().msgBytes);
  const auto payload = makeWorstCasePayload(PAYLOAD_SIZE);

  std::vector<std::uint8_t> encoded(payload.size() * 2 + 2);

  perf.setup([&] {
    auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()}, encoded.data(),
                          encoded.size());
    ASSERT_EQ(r.status, slip::Status::OK);
  });

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                                     encoded.data(), encoded.size());
      (void)r;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                              encoded.data(), encoded.size());
        ASSERT_EQ(r.status, slip::Status::OK);
      },
      "encode-worst");
}

/**
 * @brief Encode throughput with realistic escape rate (~5%).
 */
PERF_TEST(SLIPFramingPerf, EncodeRealistic) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  const std::size_t PAYLOAD_SIZE = static_cast<std::size_t>(getCfg().msgBytes);
  const auto payload = makeRealisticPayload(PAYLOAD_SIZE);

  std::vector<std::uint8_t> encoded(payload.size() * 2 + 2);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                                     encoded.data(), encoded.size());
      (void)r;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                              encoded.data(), encoded.size());
        ASSERT_EQ(r.status, slip::Status::OK);
      },
      "encode-realistic");
}

/* ----------------------------- Decode Tests ----------------------------- */

/**
 * @brief Decode throughput with clean stream (no escapes).
 */
PERF_TEST(SLIPFramingPerf, DecodeClean) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  const std::size_t PAYLOAD_SIZE = static_cast<std::size_t>(getCfg().msgBytes);
  const auto payload = makeCleanPayload(PAYLOAD_SIZE);

  std::vector<std::uint8_t> encoded(payload.size() * 2 + 2);
  auto encResult = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                                encoded.data(), encoded.size());
  ASSERT_EQ(encResult.status, slip::Status::OK);
  encoded.resize(encResult.bytesProduced);

  slip::DecodeConfig cfg{};
  std::vector<std::uint8_t> decoded(payload.size());

  perf.setup([&] {
    slip::DecodeState st{};
    auto r = slip::decodeChunk(st, cfg, apex::compat::bytes_span{encoded.data(), encoded.size()},
                               decoded.data(), decoded.size());
    ASSERT_EQ(r.status, slip::Status::OK);
    ASSERT_TRUE(r.frameCompleted);
  });

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      slip::DecodeState st{};
      volatile auto r =
          slip::decodeChunk(st, cfg, apex::compat::bytes_span{encoded.data(), encoded.size()},
                            decoded.data(), decoded.size());
      (void)r;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        slip::DecodeState st{};
        auto r =
            slip::decodeChunk(st, cfg, apex::compat::bytes_span{encoded.data(), encoded.size()},
                              decoded.data(), decoded.size());
        ASSERT_EQ(r.status, slip::Status::OK);
        ASSERT_TRUE(r.frameCompleted);
      },
      "decode-clean");
}

/**
 * @brief Decode throughput with heavily escaped stream.
 */
PERF_TEST(SLIPFramingPerf, DecodeWorstCase) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  const std::size_t PAYLOAD_SIZE = static_cast<std::size_t>(getCfg().msgBytes);
  const auto payload = makeWorstCasePayload(PAYLOAD_SIZE);

  std::vector<std::uint8_t> encoded(payload.size() * 2 + 2);
  auto encResult = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                                encoded.data(), encoded.size());
  ASSERT_EQ(encResult.status, slip::Status::OK);
  encoded.resize(encResult.bytesProduced);

  slip::DecodeConfig cfg{};
  std::vector<std::uint8_t> decoded(payload.size());

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      slip::DecodeState st{};
      volatile auto r =
          slip::decodeChunk(st, cfg, apex::compat::bytes_span{encoded.data(), encoded.size()},
                            decoded.data(), decoded.size());
      (void)r;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        slip::DecodeState st{};
        auto r =
            slip::decodeChunk(st, cfg, apex::compat::bytes_span{encoded.data(), encoded.size()},
                              decoded.data(), decoded.size());
        ASSERT_EQ(r.status, slip::Status::OK);
        ASSERT_TRUE(r.frameCompleted);
      },
      "decode-worst");
}

/**
 * @brief Decode performance with chunked delivery (streaming).
 */
PERF_TEST(SLIPFramingPerf, DecodeStreaming) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  const std::size_t PAYLOAD_SIZE = static_cast<std::size_t>(getCfg().msgBytes);
  const auto payload = makeRealisticPayload(PAYLOAD_SIZE);

  std::vector<std::uint8_t> encoded(payload.size() * 2 + 2);
  auto encResult = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                                encoded.data(), encoded.size());
  ASSERT_EQ(encResult.status, slip::Status::OK);
  encoded.resize(encResult.bytesProduced);

  constexpr std::size_t CHUNK_SIZE = 8;
  slip::DecodeConfig cfg{};
  std::vector<std::uint8_t> decoded(payload.size());

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      slip::DecodeState st{};
      std::size_t inPos = 0;
      std::size_t outPos = 0;

      while (inPos < encoded.size()) {
        const std::size_t chunkLen = std::min(CHUNK_SIZE, encoded.size() - inPos);
        volatile auto r =
            slip::decodeChunk(st, cfg, apex::compat::bytes_span{encoded.data() + inPos, chunkLen},
                              decoded.data() + outPos, decoded.size() - outPos);
        inPos += r.bytesConsumed;
        outPos += r.bytesProduced;
        if (r.frameCompleted)
          break;
      }
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        slip::DecodeState st{};
        std::size_t inPos = 0;
        std::size_t outPos = 0;

        while (inPos < encoded.size()) {
          const std::size_t chunkLen = std::min(CHUNK_SIZE, encoded.size() - inPos);
          auto r =
              slip::decodeChunk(st, cfg, apex::compat::bytes_span{encoded.data() + inPos, chunkLen},
                                decoded.data() + outPos, decoded.size() - outPos);
          inPos += r.bytesConsumed;
          outPos += r.bytesProduced;
          if (r.frameCompleted)
            break;
        }
      },
      "decode-streaming");
}

/**
 * @brief Decode throughput when processing multiple frames in sequence.
 */
PERF_TEST(SLIPFramingPerf, DecodeMultiFrame) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  const std::size_t FRAME_SIZE = static_cast<std::size_t>(getCfg().msgBytes) / 10;
  std::vector<std::uint8_t> stream;

  for (int i = 0; i < 10; ++i) {
    const auto payload = makeCleanPayload(FRAME_SIZE);
    std::vector<std::uint8_t> encoded(payload.size() * 2 + 2);
    auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()}, encoded.data(),
                          encoded.size());
    ASSERT_EQ(r.status, slip::Status::OK);
    stream.insert(stream.end(), encoded.data(), encoded.data() + r.bytesProduced);
  }

  slip::DecodeConfig cfg{};
  std::vector<std::uint8_t> decoded(FRAME_SIZE);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      slip::DecodeState st{};
      std::size_t pos = 0;
      int frameCount = 0;

      while (pos < stream.size() && frameCount < 10) {
        volatile auto r = slip::decodeChunk(
            st, cfg, apex::compat::bytes_span{stream.data() + pos, stream.size() - pos},
            decoded.data(), decoded.size());
        pos += r.bytesConsumed;
        if (r.frameCompleted)
          frameCount++;
      }
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        slip::DecodeState st{};
        std::size_t pos = 0;
        int frameCount = 0;

        while (pos < stream.size() && frameCount < 10) {
          auto r = slip::decodeChunk(
              st, cfg, apex::compat::bytes_span{stream.data() + pos, stream.size() - pos},
              decoded.data(), decoded.size());
          pos += r.bytesConsumed;
          if (r.frameCompleted)
            frameCount++;
        }

        ASSERT_EQ(frameCount, 10);
      },
      "decode-multiframe");

  const double TOTAL_SIZE = FRAME_SIZE * 10;
}

/* ----------------------------- Payload Size Sweep ----------------------------- */

class SLIPPayloadSweep : public ::testing::TestWithParam<std::size_t> {
protected:
  const ub::PerfConfig& getCfg() { return ub::detail::getPerfConfig(); }
};

/**
 * @brief Encode throughput across multiple payload sizes.
 */
TEST_P(SLIPPayloadSweep, EncodeSweep) {
  const std::size_t PAYLOAD_SIZE = GetParam();

  ub::PerfConfig cfg = getCfg();
  cfg.msgBytes = static_cast<int>(PAYLOAD_SIZE);

  std::string testName = "SLIPPayloadSweep.EncodeSweep/" + std::to_string(PAYLOAD_SIZE);
  ub::PerfCase perf{testName, cfg};
  ub::attachProfilerHooks(perf, cfg);

  const auto payload = makeCleanPayload(PAYLOAD_SIZE);
  std::vector<std::uint8_t> encoded(payload.size() * 2 + 2);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                                     encoded.data(), encoded.size());
      (void)r;
    }
  });

  ub::MemoryProfile memProfile{
      .bytesRead = PAYLOAD_SIZE, .bytesWritten = PAYLOAD_SIZE * 2 + 2, .bytesAllocated = 0};

  const auto R = perf.throughputLoop(
      [&] {
        auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                              encoded.data(), encoded.size());
        ASSERT_EQ(r.status, slip::Status::OK);
      },
      "encode-sweep", memProfile);
}

/**
 * @brief Decode throughput across multiple payload sizes.
 */
TEST_P(SLIPPayloadSweep, DecodeSweep) {
  const std::size_t PAYLOAD_SIZE = GetParam();

  ub::PerfConfig cfg = getCfg();
  cfg.msgBytes = static_cast<int>(PAYLOAD_SIZE);

  std::string testName = "SLIPPayloadSweep.DecodeSweep/" + std::to_string(PAYLOAD_SIZE);
  ub::PerfCase perf{testName, cfg};
  ub::attachProfilerHooks(perf, cfg);

  const auto payload = makeCleanPayload(PAYLOAD_SIZE);

  std::vector<std::uint8_t> encoded(payload.size() * 2 + 2);
  auto encResult = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                                encoded.data(), encoded.size());
  ASSERT_EQ(encResult.status, slip::Status::OK);
  encoded.resize(encResult.bytesProduced);

  slip::DecodeConfig cfg_slip{};
  cfg_slip.maxFrameSize = PAYLOAD_SIZE;
  std::vector<std::uint8_t> decoded(payload.size());

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      slip::DecodeState st{};
      volatile auto r =
          slip::decodeChunk(st, cfg_slip, apex::compat::bytes_span{encoded.data(), encoded.size()},
                            decoded.data(), decoded.size());
      (void)r;
    }
  });

  ub::MemoryProfile memProfile{
      .bytesRead = encoded.size(), .bytesWritten = PAYLOAD_SIZE, .bytesAllocated = 0};

  const auto R = perf.throughputLoop(
      [&] {
        slip::DecodeState st{};
        auto r = slip::decodeChunk(st, cfg_slip,
                                   apex::compat::bytes_span{encoded.data(), encoded.size()},
                                   decoded.data(), decoded.size());
        ASSERT_EQ(r.status, slip::Status::OK);
      },
      "decode-sweep", memProfile);
}

INSTANTIATE_TEST_SUITE_P(PayloadSizes, SLIPPayloadSweep,
                         ::testing::Values(64, 256, 1024, 4096, 16384, 65536, 262144));

/* ----------------------------- Cache Hierarchy ----------------------------- */

/**
 * @brief Performance with L1 cache-sized payload.
 */
PERF_TEST(SLIPCache, EncodeCacheL1) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  constexpr std::size_t L1_SIZE = 16 * 1024;
  const auto payload = makeCleanPayload(L1_SIZE);
  std::vector<std::uint8_t> encoded(payload.size() * 2 + 2);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                                     encoded.data(), encoded.size());
      (void)r;
    }
  });

  ub::MemoryProfile memProfile{
      .bytesRead = L1_SIZE, .bytesWritten = L1_SIZE * 2 + 2, .bytesAllocated = 0};

  const auto R = perf.throughputLoop(
      [&] {
        auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                              encoded.data(), encoded.size());
        ASSERT_EQ(r.status, slip::Status::OK);
      },
      "encode-L1", memProfile);
}

/**
 * @brief Performance with L3 cache-sized payload.
 */
PERF_TEST(SLIPCache, EncodeCacheL3) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  constexpr std::size_t L3_SIZE = 1 * 1024 * 1024;
  const auto payload = makeCleanPayload(L3_SIZE);
  std::vector<std::uint8_t> encoded(payload.size() * 2 + 2);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles() / 10; ++i) {
      volatile auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                                     encoded.data(), encoded.size());
      (void)r;
    }
  });

  ub::MemoryProfile memProfile{
      .bytesRead = L3_SIZE, .bytesWritten = L3_SIZE * 2 + 2, .bytesAllocated = 0};

  const auto R = perf.throughputLoop(
      [&] {
        auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                              encoded.data(), encoded.size());
        ASSERT_EQ(r.status, slip::Status::OK);
      },
      "encode-L3", memProfile);
}

/**
 * @brief Performance with RAM-sized payload (exceeds all caches).
 */
PERF_TEST(SLIPCache, EncodeCacheRAM) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  constexpr std::size_t RAM_SIZE = 4 * 1024 * 1024;
  const auto payload = makeCleanPayload(RAM_SIZE);
  std::vector<std::uint8_t> encoded(payload.size() * 2 + 2);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles() / 10; ++i) {
      volatile auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                                     encoded.data(), encoded.size());
      (void)r;
    }
  });

  ub::MemoryProfile memProfile{
      .bytesRead = RAM_SIZE, .bytesWritten = RAM_SIZE * 2 + 2, .bytesAllocated = 0};

  const auto R = perf.throughputLoop(
      [&] {
        auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                              encoded.data(), encoded.size());
        ASSERT_EQ(r.status, slip::Status::OK);
      },
      "encode-RAM", memProfile);
}

/* ----------------------------- Configuration Comparison ----------------------------- */

/**
 * @brief Compare encode performance with different delimiter configurations.
 */
PERF_TEST(SLIPConfig, EncodeDelimiterComparison) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  const std::size_t PAYLOAD_SIZE = static_cast<std::size_t>(getCfg().msgBytes);
  const auto payload = makeCleanPayload(PAYLOAD_SIZE);
  std::vector<std::uint8_t> encoded(payload.size() * 2 + 2);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                                     encoded.data(), encoded.size(), true, true);
      (void)r;
    }
  });

  const auto bothDelim = perf.throughputLoop(
      [&] {
        auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                              encoded.data(), encoded.size(), true, true);
        ASSERT_EQ(r.status, slip::Status::OK);
      },
      "both-delimiters");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                                     encoded.data(), encoded.size(), true, false);
      (void)r;
    }
  });

  const auto leadingOnly = perf.throughputLoop(
      [&] {
        auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                              encoded.data(), encoded.size(), true, false);
        ASSERT_EQ(r.status, slip::Status::OK);
      },
      "leading-only");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                                     encoded.data(), encoded.size(), false, false);
      (void)r;
    }
  });

  const auto noDelim = perf.throughputLoop(
      [&] {
        auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                              encoded.data(), encoded.size(), false, false);
        ASSERT_EQ(r.status, slip::Status::OK);
      },
      "no-delimiters");

  std::printf("\nDelimiter Configuration Comparison:\n");
  std::printf("  Both delimiters: %.3f us (%.0f MB/s)\n", bothDelim.stats.median,
              (bothDelim.callsPerSecond * PAYLOAD_SIZE) / (1024.0 * 1024.0));
  std::printf("  Leading only:    %.3f us (%.0f MB/s)\n", leadingOnly.stats.median,
              (leadingOnly.callsPerSecond * PAYLOAD_SIZE) / (1024.0 * 1024.0));
  std::printf("  No delimiters:   %.3f us (%.0f MB/s)\n", noDelim.stats.median,
              (noDelim.callsPerSecond * PAYLOAD_SIZE) / (1024.0 * 1024.0));

  const double overheadPct =
      ((bothDelim.stats.median - noDelim.stats.median) / noDelim.stats.median) * 100.0;
  std::printf("\nDelimiter overhead: %.1f%%\n", overheadPct);
}

/* ----------------------------- Encode vs Decode Comparison ----------------------------- */

/**
 * @brief Direct comparison of encode and decode performance.
 */
PERF_TEST(SLIPComparison, EncodeVsDecodePerformance) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  const std::size_t PAYLOAD_SIZE = static_cast<std::size_t>(getCfg().msgBytes);
  const auto payload = makeCleanPayload(PAYLOAD_SIZE);

  std::vector<std::uint8_t> encoded(payload.size() * 2 + 2);
  auto encResult = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                                encoded.data(), encoded.size());
  ASSERT_EQ(encResult.status, slip::Status::OK);
  encoded.resize(encResult.bytesProduced);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                                     encoded.data(), encoded.size());
      (void)r;
    }
  });

  ub::MemoryProfile encMemProfile{
      .bytesRead = PAYLOAD_SIZE, .bytesWritten = encResult.bytesProduced, .bytesAllocated = 0};

  const auto encTime = perf.throughputLoop(
      [&] {
        auto r = slip::encode(apex::compat::bytes_span{payload.data(), payload.size()},
                              encoded.data(), encoded.size());
        ASSERT_EQ(r.status, slip::Status::OK);
      },
      "encode", encMemProfile);

  slip::DecodeConfig cfg{};
  cfg.maxFrameSize = PAYLOAD_SIZE;
  std::vector<std::uint8_t> decoded(payload.size());

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      slip::DecodeState st{};
      volatile auto r =
          slip::decodeChunk(st, cfg, apex::compat::bytes_span{encoded.data(), encoded.size()},
                            decoded.data(), decoded.size());
      (void)r;
    }
  });

  ub::MemoryProfile decMemProfile{
      .bytesRead = encResult.bytesProduced, .bytesWritten = PAYLOAD_SIZE, .bytesAllocated = 0};

  const auto decTime = perf.throughputLoop(
      [&] {
        slip::DecodeState st{};
        auto r =
            slip::decodeChunk(st, cfg, apex::compat::bytes_span{encoded.data(), encoded.size()},
                              decoded.data(), decoded.size());
        ASSERT_EQ(r.status, slip::Status::OK);
      },
      "decode", decMemProfile);

  const double encMBps = (encTime.callsPerSecond * PAYLOAD_SIZE) / (1024.0 * 1024.0);
  const double decMBps = (decTime.callsPerSecond * PAYLOAD_SIZE) / (1024.0 * 1024.0);
  const double encBW = encMemProfile.bandwidthMBs(encTime.stats.median);
  const double decBW = decMemProfile.bandwidthMBs(decTime.stats.median);
  const double ratio = encTime.stats.median / decTime.stats.median;

  std::printf("\nEncode vs Decode Comparison:\n");
  std::printf("\nEncode:\n");
  std::printf("  Latency:    %.3f us\n", encTime.stats.median);
  std::printf("  Throughput: %.0f MB/s\n", encMBps);
  std::printf("  Bandwidth:  %.0f MB/s\n", encBW);
  std::printf("  CV:         %.1f%%\n", encTime.stats.cv * 100.0);

  std::printf("\nDecode:\n");
  std::printf("  Latency:    %.3f us\n", decTime.stats.median);
  std::printf("  Throughput: %.0f MB/s\n", decMBps);
  std::printf("  Bandwidth:  %.0f MB/s\n", decBW);
  std::printf("  CV:         %.1f%%\n", decTime.stats.cv * 100.0);

  std::printf("\nRatio:\n");
  std::printf("  Encode/Decode: %.2fx\n", ratio);
  if (ratio > 1.0) {
    std::printf("  Decode is %.1f%% faster\n", ((ratio - 1.0) * 100.0));
  } else {
    std::printf("  Encode is %.1f%% faster\n", ((1.0 / ratio - 1.0) * 100.0));
  }
}

PERF_MAIN()
