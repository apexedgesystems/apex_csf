/**
 * @file Gates_pTest.cpp
 * @brief Performance tests for logic gate truth tables and adder circuits.
 *
 * Measures evaluation throughput for all 7 gate types (NOT, AND, OR, NAND,
 * NOR, XOR, XNOR) and half/full adder combinational logic.
 *
 * All functions are constexpr -- expected to be trivially fast (sub-nanosecond
 * per gate evaluation). This benchmark establishes the performance floor.
 *
 * Usage:
 *   ./Gates_PTEST --csv results.csv
 *   ./Gates_PTEST --quick
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/electronics/topologies/gates/inc/LogicGates.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdio>

namespace ub = vernier::bench;
using sim::electronics::topologies::gates::fullAdder;
using sim::electronics::topologies::gates::gateAnd;
using sim::electronics::topologies::gates::gateNand;
using sim::electronics::topologies::gates::gateNor;
using sim::electronics::topologies::gates::gateNot;
using sim::electronics::topologies::gates::gateOr;
using sim::electronics::topologies::gates::gateXnor;
using sim::electronics::topologies::gates::gateXor;
using sim::electronics::topologies::gates::halfAdder;

/* ----------------------------- Gate Evaluation ----------------------------- */

/**
 * @brief All 7 gate types evaluated over 10k input pairs.
 *
 * Exercises NOT, AND, OR, NAND, NOR, XOR, XNOR in a tight loop.
 * Volatile sink prevents dead-code elimination.
 */
PERF_TEST(GateEvaluation, AllGates_10000) {
  UB_PERF_GUARD(perf);

  const std::size_t COUNT = 10000;
  volatile int sink = 0;

  std::printf("\n=== Gate Evaluation (7 gates x %zu iterations) ===\n", COUNT);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      int sum = 0;
      for (std::size_t j = 0; j < COUNT; ++j) {
        int a = static_cast<int>(j & 1);
        int b = static_cast<int>((j >> 1) & 1);
        sum += gateNot(a);
        sum += gateAnd(a, b);
        sum += gateOr(a, b);
        sum += gateNand(a, b);
        sum += gateNor(a, b);
        sum += gateXor(a, b);
        sum += gateXnor(a, b);
      }
      sink = sum;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        int sum = 0;
        for (std::size_t j = 0; j < COUNT; ++j) {
          int a = static_cast<int>(j & 1);
          int b = static_cast<int>((j >> 1) & 1);
          sum += gateNot(a);
          sum += gateAnd(a, b);
          sum += gateOr(a, b);
          sum += gateNand(a, b);
          sum += gateNor(a, b);
          sum += gateXor(a, b);
          sum += gateXnor(a, b);
        }
        sink = sum;
      },
      "all_gates_10k");

  double perGate = result.stats.median * 1000.0 / (COUNT * 7);
  std::printf("  %zu x 7 gates: %8.0f batches/s  (%.2f ns/gate)\n", COUNT, result.callsPerSecond,
              perGate);
}

/* ----------------------------- Adder Throughput ----------------------------- */

/**
 * @brief Half adder throughput over 10k evaluations.
 *
 * Half adder = XOR + AND (internally 5 NAND gates + 2 NOR gates).
 */
PERF_TEST(AdderThroughput, HalfAdder_10000) {
  UB_PERF_GUARD(perf);

  const std::size_t COUNT = 10000;
  volatile int sink = 0;

  std::printf("\n=== Half Adder Throughput (%zu iterations) ===\n", COUNT);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      int sum = 0;
      for (std::size_t j = 0; j < COUNT; ++j) {
        int a = static_cast<int>(j & 1);
        int b = static_cast<int>((j >> 1) & 1);
        auto r = halfAdder(a, b);
        sum += r.sum + r.carry;
      }
      sink = sum;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        int sum = 0;
        for (std::size_t j = 0; j < COUNT; ++j) {
          int a = static_cast<int>(j & 1);
          int b = static_cast<int>((j >> 1) & 1);
          auto r = halfAdder(a, b);
          sum += r.sum + r.carry;
        }
        sink = sum;
      },
      "half_adder_10k");

  double perOp = result.stats.median * 1000.0 / COUNT;
  std::printf("  %zu half-adders: %8.0f batches/s  (%.2f ns/op)\n", COUNT, result.callsPerSecond,
              perOp);
}

/**
 * @brief Full adder throughput over 10k evaluations.
 *
 * Full adder = 2 half adders + OR (internally ~14 primitive gates).
 */
PERF_TEST(AdderThroughput, FullAdder_10000) {
  UB_PERF_GUARD(perf);

  const std::size_t COUNT = 10000;
  volatile int sink = 0;

  std::printf("\n=== Full Adder Throughput (%zu iterations) ===\n", COUNT);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      int sum = 0;
      for (std::size_t j = 0; j < COUNT; ++j) {
        int a = static_cast<int>(j & 1);
        int b = static_cast<int>((j >> 1) & 1);
        int cin = static_cast<int>((j >> 2) & 1);
        auto r = fullAdder(a, b, cin);
        sum += r.sum + r.cout;
      }
      sink = sum;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        int sum = 0;
        for (std::size_t j = 0; j < COUNT; ++j) {
          int a = static_cast<int>(j & 1);
          int b = static_cast<int>((j >> 1) & 1);
          int cin = static_cast<int>((j >> 2) & 1);
          auto r = fullAdder(a, b, cin);
          sum += r.sum + r.cout;
        }
        sink = sum;
      },
      "full_adder_10k");

  double perOp = result.stats.median * 1000.0 / COUNT;
  std::printf("  %zu full-adders: %8.0f batches/s  (%.2f ns/op)\n", COUNT, result.callsPerSecond,
              perOp);
}

PERF_MAIN()
