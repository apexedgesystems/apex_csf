/**
 * @file DataProxy_pTest.cpp
 * @brief Performance tests for data proxy infrastructure.
 *
 * Measures:
 *  - ByteMaskProxy apply throughput and mask size scaling
 *  - EndiannessProxy scalar and struct swap throughput
 *  - MasterDataProxy passthrough vs swap vs full pipeline overhead
 *
 * Usage:
 *   ./DataProxy_PTEST --csv results.csv
 *   ./DataProxy_PTEST --quick
 *   ./DataProxy_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "src/utilities/data_proxy/inc/MasterDataProxy.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace ub = vernier::bench;

using system_core::data_proxy::ByteMaskProxy;
using system_core::data_proxy::EndiannessProxy;
using system_core::data_proxy::MasterDataProxy;

/* ----------------------------- Test Structs ----------------------------- */

namespace {

/**
 * @brief Small packet (8 bytes) - fits in registers.
 */
struct SmallPacket {
  std::uint32_t timestamp;
  std::uint16_t sensorId;
  std::uint16_t value;
};

void endianSwap(const SmallPacket& in, SmallPacket& out) noexcept {
  out.timestamp = system_core::data_proxy::swapBytes(in.timestamp);
  out.sensorId = system_core::data_proxy::swapBytes(in.sensorId);
  out.value = system_core::data_proxy::swapBytes(in.value);
}

/**
 * @brief Medium packet (32 bytes) - typical telemetry.
 */
struct MediumPacket {
  std::uint64_t timestamp;
  std::uint32_t sensorId;
  std::uint32_t sequenceNum;
  std::array<std::uint16_t, 8> values;
};

void endianSwap(const MediumPacket& in, MediumPacket& out) noexcept {
  out.timestamp = system_core::data_proxy::swapBytes(in.timestamp);
  out.sensorId = system_core::data_proxy::swapBytes(in.sensorId);
  out.sequenceNum = system_core::data_proxy::swapBytes(in.sequenceNum);
  for (std::size_t i = 0; i < 8; ++i) {
    out.values[i] = system_core::data_proxy::swapBytes(in.values[i]);
  }
}

/**
 * @brief Large packet (128 bytes) - full state vector.
 */
struct LargePacket {
  std::uint64_t timestamp;
  std::uint64_t frameId;
  std::array<double, 14> stateVector; // 112 bytes
};

void endianSwap(const LargePacket& in, LargePacket& out) noexcept {
  out.timestamp = system_core::data_proxy::swapBytes(in.timestamp);
  out.frameId = system_core::data_proxy::swapBytes(in.frameId);
  for (std::size_t i = 0; i < 14; ++i) {
    out.stateVector[i] = system_core::data_proxy::swapBytes(in.stateVector[i]);
  }
}

} // namespace

/* ----------------------------- ByteMaskProxy ----------------------------- */

/**
 * @brief Measure ByteMaskProxy apply() latency.
 */
PERF_TEST(ByteMaskProxy, ApplyLatency) {
  UB_PERF_GUARD(perf);

  std::array<std::uint8_t, 64> data{};
  data.fill(0xFF);

  ByteMaskProxy proxy;
  (void)proxy.pushZeroMask(0, 16);

  std::printf("\n=== ByteMaskProxy Apply Latency ===\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      // Reset data for next iteration
      data.fill(0xFF);
      (void)proxy.apply(data.data(), data.size());
    }
  });

  auto result =
      perf.throughputLoop([&] { (void)proxy.apply(data.data(), data.size()); }, "apply_16b");

  std::printf("Apply 16 bytes: %8.0f ops/s  (%.1f ns/call)\n", result.callsPerSecond,
              result.stats.median * 1000.0);
}

/**
 * @brief Measure mask size impact on performance.
 */
PERF_TEST(ByteMaskProxy, MaskSizeScaling) {
  UB_PERF_GUARD(perf);

  std::array<std::uint8_t, 64> data{};

  std::printf("\n=== ByteMaskProxy Mask Size Scaling ===\n");
  std::printf("%-12s %12s %12s\n", "Mask Size", "ops/s", "ns/call");
  std::printf("%s\n", std::string(40, '-').c_str());

  for (std::uint8_t maskLen : std::array<std::uint8_t, 5>{1, 4, 8, 16, 32}) {
    ByteMaskProxy proxy;
    (void)proxy.pushZeroMask(0, maskLen);
    data.fill(0xFF);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        (void)proxy.apply(data.data(), data.size());
      }
    });

    std::string label = std::to_string(maskLen) + "B";
    auto result =
        perf.throughputLoop([&] { (void)proxy.apply(data.data(), data.size()); }, label.c_str());

    std::printf("%-12s %12.0f %12.1f\n", label.c_str(), result.callsPerSecond,
                result.stats.median * 1000.0);
  }
}

/* ----------------------------- EndiannessProxy ----------------------------- */

/**
 * @brief Measure scalar endianness swap performance.
 */
PERF_TEST(EndiannessProxy, ScalarSwap) {
  UB_PERF_GUARD(perf);

  std::printf("\n=== EndiannessProxy Scalar Swap ===\n");
  std::printf("%-12s %12s %12s\n", "Type", "ops/s", "ns/call");
  std::printf("%s\n", std::string(40, '-').c_str());

  // uint32_t
  {
    std::uint32_t in = 0x12345678;
    std::uint32_t out = 0;
    EndiannessProxy<std::uint32_t, true> proxy(&in, &out);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        (void)proxy.resolve();
      }
    });

    auto result = perf.throughputLoop([&] { (void)proxy.resolve(); }, "u32");

    std::printf("%-12s %12.0f %12.1f\n", "uint32_t", result.callsPerSecond,
                result.stats.median * 1000.0);
  }

  // uint64_t
  {
    std::uint64_t in = 0x123456789ABCDEF0ULL;
    std::uint64_t out = 0;
    EndiannessProxy<std::uint64_t, true> proxy(&in, &out);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        (void)proxy.resolve();
      }
    });

    auto result = perf.throughputLoop([&] { (void)proxy.resolve(); }, "u64");

    std::printf("%-12s %12.0f %12.1f\n", "uint64_t", result.callsPerSecond,
                result.stats.median * 1000.0);
  }

  // double
  {
    double in = 3.14159265358979;
    double out = 0.0;
    EndiannessProxy<double, true> proxy(&in, &out);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        (void)proxy.resolve();
      }
    });

    auto result = perf.throughputLoop([&] { (void)proxy.resolve(); }, "f64");

    std::printf("%-12s %12.0f %12.1f\n", "double", result.callsPerSecond,
                result.stats.median * 1000.0);
  }
}

/**
 * @brief Measure struct endianness swap performance via ADL.
 */
PERF_TEST(EndiannessProxy, StructSwap) {
  UB_PERF_GUARD(perf);

  std::printf("\n=== EndiannessProxy Struct Swap (ADL) ===\n");
  std::printf("%-16s %8s %12s %12s\n", "Struct", "Bytes", "ops/s", "ns/call");
  std::printf("%s\n", std::string(52, '-').c_str());

  // SmallPacket (8 bytes)
  {
    SmallPacket in{0x12345678, 0xABCD, 0x1234};
    SmallPacket out{};
    EndiannessProxy<SmallPacket, true> proxy(&in, &out);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        (void)proxy.resolve();
      }
    });

    auto result = perf.throughputLoop([&] { (void)proxy.resolve(); }, "small");

    std::printf("%-16s %8zu %12.0f %12.1f\n", "SmallPacket", sizeof(SmallPacket),
                result.callsPerSecond, result.stats.median * 1000.0);
  }

  // MediumPacket (32 bytes)
  {
    MediumPacket in{};
    in.timestamp = 0x123456789ABCDEF0ULL;
    in.sensorId = 0x12345678;
    in.sequenceNum = 0xABCDEF01;
    in.values.fill(0x1234);

    MediumPacket out{};
    EndiannessProxy<MediumPacket, true> proxy(&in, &out);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        (void)proxy.resolve();
      }
    });

    auto result = perf.throughputLoop([&] { (void)proxy.resolve(); }, "medium");

    std::printf("%-16s %8zu %12.0f %12.1f\n", "MediumPacket", sizeof(MediumPacket),
                result.callsPerSecond, result.stats.median * 1000.0);
  }

  // LargePacket (128 bytes)
  {
    LargePacket in{};
    in.timestamp = 0x123456789ABCDEF0ULL;
    in.frameId = 0xFEDCBA9876543210ULL;
    in.stateVector.fill(3.14159265358979);

    LargePacket out{};
    EndiannessProxy<LargePacket, true> proxy(&in, &out);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        (void)proxy.resolve();
      }
    });

    auto result = perf.throughputLoop([&] { (void)proxy.resolve(); }, "large");

    std::printf("%-16s %8zu %12.0f %12.1f\n", "LargePacket", sizeof(LargePacket),
                result.callsPerSecond, result.stats.median * 1000.0);
  }
}

/* ----------------------------- MasterDataProxy ----------------------------- */

/**
 * @brief Compare passthrough vs swap overhead in MasterDataProxy.
 */
PERF_TEST(MasterDataProxy, PassthroughVsSwap) {
  UB_PERF_GUARD(perf);

  SmallPacket input{0x12345678, 0xABCD, 0x1234};

  std::printf("\n=== MasterDataProxy Passthrough vs Swap ===\n");

  // Passthrough (no transformations)
  {
    MasterDataProxy<SmallPacket, false, false> proxy(&input);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        (void)proxy.resolve();
      }
    });

    auto result = perf.throughputLoop([&] { (void)proxy.resolve(); }, "passthrough");

    std::printf("Passthrough:  %10.0f ops/s  (%.1f ns/call)\n", result.callsPerSecond,
                result.stats.median * 1000.0);
  }

  // Swap only
  {
    MasterDataProxy<SmallPacket, true, false> proxy(&input);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        (void)proxy.resolve();
      }
    });

    auto result = perf.throughputLoop([&] { (void)proxy.resolve(); }, "swap");

    std::printf("Swap only:    %10.0f ops/s  (%.1f ns/call)\n", result.callsPerSecond,
                result.stats.median * 1000.0);
  }
}

/**
 * @brief Measure byte mask overhead in MasterDataProxy.
 */
PERF_TEST(MasterDataProxy, ByteMaskOverhead) {
  UB_PERF_GUARD(perf);

  SmallPacket input{0x12345678, 0xABCD, 0x1234};

  std::printf("\n=== MasterDataProxy Byte Mask Overhead ===\n");

  // Masks disabled (should be no overhead)
  {
    MasterDataProxy<SmallPacket, false, true> proxy(&input);
    proxy.setMasksEnabled(false);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        (void)proxy.resolve();
      }
    });

    auto result = perf.throughputLoop([&] { (void)proxy.resolve(); }, "disabled");

    std::printf("Masks disabled: %10.0f ops/s  (%.1f ns/call)\n", result.callsPerSecond,
                result.stats.median * 1000.0);
  }

  // Masks enabled with mask
  {
    MasterDataProxy<SmallPacket, false, true> proxy(&input);
    (void)proxy.pushZeroMask(0, 4);
    proxy.setMasksEnabled(true);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        (void)proxy.resolve();
      }
    });

    auto result = perf.throughputLoop([&] { (void)proxy.resolve(); }, "enabled");

    std::printf("Masks enabled:  %10.0f ops/s  (%.1f ns/call)\n", result.callsPerSecond,
                result.stats.median * 1000.0);
  }
}

/**
 * @brief Measure full pipeline (swap + masks) performance.
 */
PERF_TEST(MasterDataProxy, FullPipeline) {
  UB_PERF_GUARD(perf);

  std::printf("\n=== MasterDataProxy Full Pipeline (Swap + Masks) ===\n");
  std::printf("%-16s %8s %12s %12s\n", "Struct", "Bytes", "ops/s", "ns/call");
  std::printf("%s\n", std::string(52, '-').c_str());

  // SmallPacket
  {
    SmallPacket input{0x12345678, 0xABCD, 0x1234};
    MasterDataProxy<SmallPacket, true, true> proxy(&input);
    (void)proxy.pushZeroMask(0, 4);
    proxy.setMasksEnabled(true);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        (void)proxy.resolve();
      }
    });

    auto result = perf.throughputLoop([&] { (void)proxy.resolve(); }, "small");

    std::printf("%-16s %8zu %12.0f %12.1f\n", "SmallPacket", sizeof(SmallPacket),
                result.callsPerSecond, result.stats.median * 1000.0);
  }

  // MediumPacket
  {
    MediumPacket input{};
    input.timestamp = 0x123456789ABCDEF0ULL;
    MasterDataProxy<MediumPacket, true, true> proxy(&input);
    (void)proxy.pushZeroMask(0, 8);
    proxy.setMasksEnabled(true);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        (void)proxy.resolve();
      }
    });

    auto result = perf.throughputLoop([&] { (void)proxy.resolve(); }, "medium");

    std::printf("%-16s %8zu %12.0f %12.1f\n", "MediumPacket", sizeof(MediumPacket),
                result.callsPerSecond, result.stats.median * 1000.0);
  }

  // LargePacket
  {
    LargePacket input{};
    input.timestamp = 0x123456789ABCDEF0ULL;
    input.stateVector.fill(3.14159);
    MasterDataProxy<LargePacket, true, true> proxy(&input);
    (void)proxy.pushZeroMask(0, 16);
    proxy.setMasksEnabled(true);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        (void)proxy.resolve();
      }
    });

    auto result = perf.throughputLoop([&] { (void)proxy.resolve(); }, "large");

    std::printf("%-16s %8zu %12.0f %12.1f\n", "LargePacket", sizeof(LargePacket),
                result.callsPerSecond, result.stats.median * 1000.0);
  }
}

PERF_MAIN()
