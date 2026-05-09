/**
 * @file Intel4004Behavioral_pTest.cpp
 * @brief Performance tests for Intel 4004 behavioral CPU model.
 *
 * Measures throughput of single instruction execution (step) and
 * multi-instruction program execution (run). The behavioral model
 * is the L0 golden reference: pure C++ with no allocation in the
 * hot path.
 *
 * Usage:
 *   ./Intel4004Behavioral_PTEST --csv results.csv
 *   ./Intel4004Behavioral_PTEST --quick
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/electronics/chips/intel4004/behavioral/inc/Intel4004Cpu.hpp"
#include "src/sim/electronics/chips/intel4004/behavioral/inc/Intel4004Instructions.hpp"
#include "src/sim/electronics/chips/intel4004/behavioral/inc/Intel4004Programs.hpp"

#include <gtest/gtest.h>

#include <cstdio>

namespace ub = vernier::bench;
using sim::electronics::chips::intel4004::Intel4004Cpu;
using namespace sim::electronics::chips::intel4004;

/* ----------------------------- CpuStep ----------------------------- */

/**
 * @brief Single instruction execution throughput.
 *
 * Measures the cost of a single step() call executing a NOP instruction.
 * NOP is the cheapest path through the fetch-decode-execute switch, so
 * this establishes a lower bound on per-instruction overhead.
 */
PERF_TEST(BehavioralPerf, CpuStep) {
  UB_PERF_GUARD(perf);

  Intel4004Cpu cpu;
  cpu.loadProgram(PROGRAM_NOP.data(), PROGRAM_NOP.size());

  volatile std::size_t sink = 0;

  std::printf("\n=== CpuStep (single NOP instruction) ===\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      cpu.reset();
      cpu.loadProgram(PROGRAM_NOP.data(), PROGRAM_NOP.size());
      for (std::size_t s = 0; s < 8; ++s) {
        cpu.step();
      }
      sink = cpu.cyclesExecuted;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        for (int i = 0; i < perf.cycles(); ++i) {
          cpu.reset();
          cpu.loadProgram(PROGRAM_NOP.data(), PROGRAM_NOP.size());
          for (std::size_t s = 0; s < 8; ++s) {
            cpu.step();
          }
          sink = cpu.cyclesExecuted;
        }
      },
      "cpu_step_nop_8x");

  const double PER_STEP_NS = result.stats.median / (perf.cycles() * 8) * 1e3;
  std::printf("  8 NOPs per iter: %8.0f iters/s  (%.1f ns/step)\n",
              result.callsPerSecond * perf.cycles(), PER_STEP_NS);
}

/* ----------------------------- CpuProgram ----------------------------- */

/**
 * @brief Multi-instruction program execution throughput.
 *
 * Runs the ISZ counting loop program (16 iterations of ISZ + 1 FIM setup
 * = 17 instructions). This exercises register operations, conditional
 * branching, and PC manipulation -- a representative workload.
 */
PERF_TEST(BehavioralPerf, CpuProgram) {
  UB_PERF_GUARD(perf);

  Intel4004Cpu cpu;

  volatile std::size_t sink = 0;

  std::printf("\n=== CpuProgram (ISZ counting loop, 17 instructions) ===\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      cpu.reset();
      cpu.loadProgram(PROGRAM_COUNTING_LOOP.data(), PROGRAM_COUNTING_LOOP.size());
      cpu.run(100);
      sink = cpu.instructionsExecuted;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        for (int i = 0; i < perf.cycles(); ++i) {
          cpu.reset();
          cpu.loadProgram(PROGRAM_COUNTING_LOOP.data(), PROGRAM_COUNTING_LOOP.size());
          cpu.run(100);
          sink = cpu.instructionsExecuted;
        }
      },
      "cpu_program_isz_loop");

  const double PER_RUN_US = result.stats.median / perf.cycles();
  const double PER_INSTR_NS = (result.stats.median / perf.cycles()) * 1e3 / 17.0;
  std::printf("  17-instr loop: %8.0f runs/s  (%.1f us/run, %.1f ns/instr)\n",
              result.callsPerSecond * perf.cycles(), PER_RUN_US, PER_INSTR_NS);
}

PERF_MAIN()
