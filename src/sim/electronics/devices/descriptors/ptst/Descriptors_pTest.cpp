/**
 * @file Descriptors_pTest.cpp
 * @brief Performance tests for device descriptor construction and access.
 *
 * Descriptors are POD structs -- construction is a memcpy, field access is a
 * pointer dereference. These tests establish that descriptor overhead is
 * negligible compared to the physics models that consume them.
 *
 * Usage:
 *   ./Descriptors_PTEST --csv results.csv
 *   ./Descriptors_PTEST --quick
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/electronics/devices/descriptors/inc/Descriptors.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdio>
#include <vector>

namespace ub = vernier::bench;
using sim::electronics::algorithms::mna::NetID;
using sim::electronics::devices::descriptors::MosfetDescriptor;
using sim::electronics::devices::descriptors::ResistorDescriptor;

/* ----------------------------- Construction ----------------------------- */

/**
 * @brief Bulk descriptor construction throughput.
 *
 * Creates 2242 MosfetDescriptors (matching the Intel 4004 transistor count)
 * to measure per-descriptor construction cost.
 */
PERF_TEST(DescriptorPerf, MosfetConstruction_2242) {
  UB_PERF_GUARD(perf);

  const std::size_t COUNT = 2242;
  std::vector<MosfetDescriptor> descs(COUNT);
  volatile double sink = 0.0;

  std::printf("\n=== MOSFET Descriptor Construction (%zu descriptors) ===\n", COUNT);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (std::size_t j = 0; j < COUNT; ++j) {
        descs[j] = MosfetDescriptor{static_cast<NetID>(j + 1),
                                    static_cast<NetID>(j % 100),
                                    static_cast<NetID>(0),
                                    static_cast<NetID>(0),
                                    10e-6,
                                    1e-6};
      }
      sink = descs[0].W;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        for (std::size_t j = 0; j < COUNT; ++j) {
          descs[j] = MosfetDescriptor{static_cast<NetID>(j + 1),
                                      static_cast<NetID>(j % 100),
                                      static_cast<NetID>(0),
                                      static_cast<NetID>(0),
                                      10e-6,
                                      1e-6};
        }
        sink = descs[0].W;
      },
      "mosfet_2242");

  std::printf("  %zu MOSFETs: %8.0f batches/s  (%.3f us/batch, %.1f ns/descriptor)\n", COUNT,
              result.callsPerSecond, result.stats.median, result.stats.median * 1000.0 / COUNT);
}

/**
 * @brief Resistor descriptor construction for comparison.
 */
PERF_TEST(DescriptorPerf, ResistorConstruction_1000) {
  UB_PERF_GUARD(perf);

  const std::size_t COUNT = 1000;
  std::vector<ResistorDescriptor> descs(COUNT);
  volatile double sink = 0.0;

  std::printf("\n=== Resistor Descriptor Construction (%zu descriptors) ===\n", COUNT);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (std::size_t j = 0; j < COUNT; ++j) {
        descs[j] = ResistorDescriptor{static_cast<NetID>(j), static_cast<NetID>(j + 1), 1e3};
      }
      sink = descs[0].resistance;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        for (std::size_t j = 0; j < COUNT; ++j) {
          descs[j] = ResistorDescriptor{static_cast<NetID>(j), static_cast<NetID>(j + 1), 1e3};
        }
        sink = descs[0].resistance;
      },
      "resistor_1000");

  std::printf("  %zu resistors: %8.0f batches/s  (%.3f us/batch, %.1f ns/descriptor)\n", COUNT,
              result.callsPerSecond, result.stats.median, result.stats.median * 1000.0 / COUNT);
}

PERF_MAIN()
