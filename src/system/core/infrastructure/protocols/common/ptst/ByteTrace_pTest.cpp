/**
 * @file ByteTrace_pTest.cpp
 * @brief Performance tests for ByteTrace format helpers and invoke overhead.
 *
 * Measures:
 *  - formatBytesHex throughput at 4B / 32B / 64B payloads
 *  - formatBytesHex truncation path performance
 *  - formatTraceMessage full-format throughput
 *  - invokeTrace dispatch overhead when tracing is enabled
 *
 * Usage:
 *   ./ByteTrace_PTEST --csv results.csv
 *   ./ByteTrace_PTEST --quick
 *   ./ByteTrace_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/infrastructure/protocols/common/inc/ByteTrace.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace ub = vernier::bench;

using apex::protocols::ByteTrace;
using apex::protocols::formatBytesHex;
using apex::protocols::formatTraceMessage;
using apex::protocols::TraceDirection;

/* ----------------------------- Local Helpers ----------------------------- */

namespace {

/** @brief Concrete subclass exposing protected invokeTrace for benchmarking. */
class BenchTrace : public ByteTrace {
public:
  using ByteTrace::invokeTrace;
};

/** @brief No-op trace callback used to measure dispatch overhead. */
void noopCallback(TraceDirection, const std::uint8_t*, std::size_t, void*) noexcept {}

/** @brief Generate 256 bytes of deterministic test data. */
inline std::array<std::uint8_t, 256> makeTestData() {
  std::array<std::uint8_t, 256> data{};
  for (std::size_t i = 0; i < data.size(); ++i) {
    data[i] = static_cast<std::uint8_t>(i);
  }
  return data;
}

} // namespace

/* ----------------------------- Format Hex ----------------------------- */

/**
 * @brief formatBytesHex throughput with 4-byte payload.
 */
PERF_TEST(ByteTraceFormat, FormatHex4B) {
  UB_PERF_GUARD(perf);

  const auto DATA = makeTestData();
  char buf[128];

  perf.setup([&] {
    auto len = formatBytesHex(DATA.data(), 4, buf, sizeof(buf));
    ASSERT_GT(len, 0u);
  });

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      formatBytesHex(DATA.data(), 4, buf, sizeof(buf));
    }
  });

  perf.throughputLoop([&] { formatBytesHex(DATA.data(), 4, buf, sizeof(buf)); }, "format-hex-4B");
}

/**
 * @brief formatBytesHex throughput with 32-byte payload (default maxBytes).
 */
PERF_TEST(ByteTraceFormat, FormatHex32B) {
  UB_PERF_GUARD(perf);

  const auto DATA = makeTestData();
  char buf[256];

  perf.setup([&] {
    auto len = formatBytesHex(DATA.data(), 32, buf, sizeof(buf));
    ASSERT_GT(len, 0u);
  });

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      formatBytesHex(DATA.data(), 32, buf, sizeof(buf));
    }
  });

  perf.throughputLoop([&] { formatBytesHex(DATA.data(), 32, buf, sizeof(buf)); }, "format-hex-32B");
}

/**
 * @brief formatBytesHex throughput with 64-byte payload.
 */
PERF_TEST(ByteTraceFormat, FormatHex64B) {
  UB_PERF_GUARD(perf);

  const auto DATA = makeTestData();
  char buf[512];

  perf.setup([&] {
    auto len = formatBytesHex(DATA.data(), 64, buf, sizeof(buf), 64);
    ASSERT_GT(len, 0u);
  });

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      formatBytesHex(DATA.data(), 64, buf, sizeof(buf), 64);
    }
  });

  perf.throughputLoop([&] { formatBytesHex(DATA.data(), 64, buf, sizeof(buf), 64); },
                      "format-hex-64B");
}

/**
 * @brief formatBytesHex truncation + ellipsis path (64B data, maxBytes=8).
 */
PERF_TEST(ByteTraceFormat, FormatHexTruncated) {
  UB_PERF_GUARD(perf);

  const auto DATA = makeTestData();
  char buf[128];

  perf.setup([&] {
    auto len = formatBytesHex(DATA.data(), 64, buf, sizeof(buf), 8);
    ASSERT_GT(len, 0u);
  });

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      formatBytesHex(DATA.data(), 64, buf, sizeof(buf), 8);
    }
  });

  perf.throughputLoop([&] { formatBytesHex(DATA.data(), 64, buf, sizeof(buf), 8); },
                      "format-hex-truncated");
}

/* ----------------------------- Trace Message ----------------------------- */

/**
 * @brief formatTraceMessage throughput with 4-byte payload.
 */
PERF_TEST(ByteTraceMessage, Message4B) {
  UB_PERF_GUARD(perf);

  const auto DATA = makeTestData();
  char buf[256];

  perf.setup([&] {
    auto len = formatTraceMessage(TraceDirection::TX, DATA.data(), 4, buf, sizeof(buf), "TCP");
    ASSERT_GT(len, 0u);
  });

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      formatTraceMessage(TraceDirection::TX, DATA.data(), 4, buf, sizeof(buf), "TCP");
    }
  });

  perf.throughputLoop(
      [&] { formatTraceMessage(TraceDirection::TX, DATA.data(), 4, buf, sizeof(buf), "TCP"); },
      "trace-message-4B");
}

/**
 * @brief formatTraceMessage throughput with 32-byte payload.
 */
PERF_TEST(ByteTraceMessage, Message32B) {
  UB_PERF_GUARD(perf);

  const auto DATA = makeTestData();
  char buf[512];

  perf.setup([&] {
    auto len = formatTraceMessage(TraceDirection::RX, DATA.data(), 32, buf, sizeof(buf), "CAN");
    ASSERT_GT(len, 0u);
  });

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      formatTraceMessage(TraceDirection::RX, DATA.data(), 32, buf, sizeof(buf), "CAN");
    }
  });

  perf.throughputLoop(
      [&] { formatTraceMessage(TraceDirection::RX, DATA.data(), 32, buf, sizeof(buf), "CAN"); },
      "trace-message-32B");
}

/* ----------------------------- Invoke Overhead ----------------------------- */

/**
 * @brief invokeTrace dispatch overhead with noop callback (tracing enabled).
 */
PERF_TEST(ByteTraceOverhead, InvokeEnabled) {
  UB_PERF_GUARD(perf);

  BenchTrace trace;
  trace.attachTrace(noopCallback);
  trace.setTraceEnabled(true);

  const auto DATA = makeTestData();

  perf.setup([&] { trace.invokeTrace(TraceDirection::TX, DATA.data(), 32); });

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      trace.invokeTrace(TraceDirection::TX, DATA.data(), 32);
    }
  });

  perf.throughputLoop([&] { trace.invokeTrace(TraceDirection::TX, DATA.data(), 32); },
                      "invoke-overhead");
}

PERF_MAIN()
