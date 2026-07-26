/**
 * @file ParamBank_pTest.cpp
 * @brief Performance tests for ParamBank<TParams> A/B staging hot paths.
 *
 * Measures:
 *  - active() RT-critical latency (single atomic acquire load)
 *  - load(struct) throughput at small/medium/large param sizes
 *  - apply() hot-reload latency (atomic swap)
 *  - rollback() recovery latency (atomic swap)
 *  - Param size scaling
 *
 * Usage:
 *   ./ParamBank_PTEST --csv results.csv
 *   ./ParamBank_PTEST --quick
 *   ./ParamBank_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/ParamBank.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace ub = vernier::bench;

using system_core::system_component::ParamBank;
using system_core::system_component::Status;

/* ----------------------------- Test Fixtures ----------------------------- */

namespace {

/** @brief Small POD parameter struct (24 bytes). */
struct SmallParams {
  double gain{1.0};
  double offset{0.0};
  std::int32_t mode{0};
  std::int32_t pad{0};
};
static_assert(sizeof(SmallParams) == 24, "SmallParams should be 24 bytes");

/** @brief Medium POD parameter struct (88 bytes). */
struct MediumParams {
  double values[8]{};
  std::int32_t modes[4]{};
  std::uint32_t flags{0};
  std::uint32_t pad{0};
};
static_assert(sizeof(MediumParams) == 88, "MediumParams should be 88 bytes");

/** @brief Large POD parameter struct (320 bytes). */
struct LargeParams {
  double matrix[16]{};
  double vector[8]{};
  std::int32_t config[32]{};
};
static_assert(sizeof(LargeParams) == 320, "LargeParams should be 320 bytes");

} // namespace

/* ----------------------------- Active Params Access ----------------------------- */

/**
 * @brief active() latency on small params (RT-critical path).
 */
PERF_TEST(ParamBankPerf, ActiveSmall) {
  UB_PERF_GUARD(perf);

  ParamBank<SmallParams> bank;
  (void)bank.load(SmallParams{2.0, 1.0, 1, 0});
  (void)bank.publishInitial();

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      const auto& ref = bank.active();
      asm volatile("" ::"r"(&ref));
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        const auto& ref = bank.active();
        asm volatile("" ::"r"(&ref));
      },
      "active-small");

  std::printf("\n[RT-Critical] active(24B): %.3f ns (%.0f M calls/sec)\n",
              result.stats.median * 1000.0, result.callsPerSecond / 1e6);
}

/**
 * @brief active() latency on medium params.
 */
PERF_TEST(ParamBankPerf, ActiveMedium) {
  UB_PERF_GUARD(perf);

  ParamBank<MediumParams> bank;
  (void)bank.load(MediumParams{});
  (void)bank.publishInitial();

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      const auto& ref = bank.active();
      asm volatile("" ::"r"(&ref));
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        const auto& ref = bank.active();
        asm volatile("" ::"r"(&ref));
      },
      "active-medium");

  std::printf("\n[RT-Critical] active(88B): %.3f ns (%.0f M calls/sec)\n",
              result.stats.median * 1000.0, result.callsPerSecond / 1e6);
}

/**
 * @brief active() latency on large params.
 */
PERF_TEST(ParamBankPerf, ActiveLarge) {
  UB_PERF_GUARD(perf);

  ParamBank<LargeParams> bank;
  (void)bank.load(LargeParams{});
  (void)bank.publishInitial();

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      const auto& ref = bank.active();
      asm volatile("" ::"r"(&ref));
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        const auto& ref = bank.active();
        asm volatile("" ::"r"(&ref));
      },
      "active-large");

  std::printf("\n[RT-Critical] active(320B): %.3f ns (%.0f M calls/sec)\n",
              result.stats.median * 1000.0, result.callsPerSecond / 1e6);
}

/* ----------------------------- Staging Throughput ----------------------------- */

/**
 * @brief load(struct) throughput on small params (memcpy + validate).
 */
PERF_TEST(ParamBankPerf, LoadSmall) {
  UB_PERF_GUARD(perf);

  ParamBank<SmallParams> bank;
  const SmallParams P{2.0, 1.0, 1, 0};

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      auto status = bank.load(P);
      asm volatile("" ::"r"(&status));
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        auto status = bank.load(P);
        asm volatile("" ::"r"(&status));
      },
      "load-small");

  std::printf("\n[Cold-Path] load(24B): %.3f ns (%.0f M calls/sec)\n", result.stats.median * 1000.0,
              result.callsPerSecond / 1e6);
}

/**
 * @brief load(struct) throughput on medium params.
 */
PERF_TEST(ParamBankPerf, LoadMedium) {
  UB_PERF_GUARD(perf);

  ParamBank<MediumParams> bank;
  const MediumParams P{};

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      auto status = bank.load(P);
      asm volatile("" ::"r"(&status));
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        auto status = bank.load(P);
        asm volatile("" ::"r"(&status));
      },
      "load-medium");

  std::printf("\n[Cold-Path] load(88B): %.3f ns (%.0f M calls/sec)\n", result.stats.median * 1000.0,
              result.callsPerSecond / 1e6);
}

/**
 * @brief load(struct) throughput on large params.
 */
PERF_TEST(ParamBankPerf, LoadLarge) {
  UB_PERF_GUARD(perf);

  ParamBank<LargeParams> bank;
  const LargeParams P{};

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      auto status = bank.load(P);
      asm volatile("" ::"r"(&status));
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        auto status = bank.load(P);
        asm volatile("" ::"r"(&status));
      },
      "load-large");

  std::printf("\n[Cold-Path] load(320B): %.3f ns (%.0f M calls/sec)\n",
              result.stats.median * 1000.0, result.callsPerSecond / 1e6);
}

/* ----------------------------- Hot Reload ----------------------------- */

/**
 * @brief load+apply hot-reload cycle on small params.
 */
PERF_TEST(ParamBankPerf, ApplySmall) {
  UB_PERF_GUARD(perf);

  ParamBank<SmallParams> bank;
  const SmallParams P1{2.0, 1.0, 1, 0};
  const SmallParams P2{3.0, 2.0, 2, 0};
  (void)bank.load(P1);
  (void)bank.publishInitial();

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)bank.load(P2);
      auto status = bank.apply();
      asm volatile("" ::"r"(&status));
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        (void)bank.load(P2);
        auto status = bank.apply();
        asm volatile("" ::"r"(&status));
      },
      "load-apply-small");

  std::printf("\n[Hot-Reload] load+apply(24B): %.3f ns (%.0f M calls/sec)\n",
              result.stats.median * 1000.0, result.callsPerSecond / 1e6);
}

/* ----------------------------- Recovery ----------------------------- */

/**
 * @brief rollback+load+apply recovery cycle on small params.
 */
PERF_TEST(ParamBankPerf, RollbackSmall) {
  UB_PERF_GUARD(perf);

  ParamBank<SmallParams> bank;
  const SmallParams P1{2.0, 1.0, 1, 0};
  const SmallParams P2{3.0, 2.0, 2, 0};
  (void)bank.load(P1);
  (void)bank.publishInitial();
  (void)bank.load(P2);
  (void)bank.apply();

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)bank.rollback();
      (void)bank.load(P2);
      (void)bank.apply();
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        (void)bank.rollback();
        (void)bank.load(P2);
        (void)bank.apply();
      },
      "rollback-load-apply-small");

  std::printf("\n[Recovery] rollback+load+apply(24B): %.3f ns (%.0f M calls/sec)\n",
              result.stats.median * 1000.0, result.callsPerSecond / 1e6);
}

/* ----------------------------- Full Cycle ----------------------------- */

/**
 * @brief Cold-path full cycle: ctor + load + publishInitial + active read.
 */
PERF_TEST(ParamBankPerf, FullPublishCycle) {
  UB_PERF_GUARD(perf);

  const SmallParams P{2.0, 1.0, 1, 0};

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      ParamBank<SmallParams> bank;
      (void)bank.load(P);
      (void)bank.publishInitial();
      const auto& ref = bank.active();
      asm volatile("" ::"r"(&ref));
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        ParamBank<SmallParams> bank;
        (void)bank.load(P);
        (void)bank.publishInitial();
        const auto& ref = bank.active();
        asm volatile("" ::"r"(&ref));
      },
      "full-publish-cycle");

  std::printf("\n[Cold-Path] ctor+load+publishInitial+active: %.3f ns (%.0f M cycles/sec)\n",
              result.stats.median * 1000.0, result.callsPerSecond / 1e6);
}

/* ----------------------------- Scaling ----------------------------- */

/**
 * @brief active() latency by param size (should be O(1): pointer load).
 */
PERF_TEST(ParamBankPerf, ParamSizeScaling) {
  UB_PERF_GUARD(perf);

  std::printf("\n[Scaling] active() by param size:\n");
  std::printf("%-12s %-15s %-15s\n", "Size(B)", "Median(ns)", "Calls/sec");
  std::printf("%s\n", std::string(45, '-').c_str());

  {
    ParamBank<SmallParams> bank;
    (void)bank.load(SmallParams{});
    (void)bank.publishInitial();
    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        const auto& ref = bank.active();
        asm volatile("" ::"r"(&ref));
      }
    });
    auto result = perf.throughputLoop(
        [&] {
          const auto& ref = bank.active();
          asm volatile("" ::"r"(&ref));
        },
        "scaling-24B");
    std::printf("%-12d %-15.3f %-15.0f\n", 24, result.stats.median * 1000.0, result.callsPerSecond);
  }

  {
    ParamBank<MediumParams> bank;
    (void)bank.load(MediumParams{});
    (void)bank.publishInitial();
    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        const auto& ref = bank.active();
        asm volatile("" ::"r"(&ref));
      }
    });
    auto result = perf.throughputLoop(
        [&] {
          const auto& ref = bank.active();
          asm volatile("" ::"r"(&ref));
        },
        "scaling-88B");
    std::printf("%-12d %-15.3f %-15.0f\n", 88, result.stats.median * 1000.0, result.callsPerSecond);
  }

  {
    ParamBank<LargeParams> bank;
    (void)bank.load(LargeParams{});
    (void)bank.publishInitial();
    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        const auto& ref = bank.active();
        asm volatile("" ::"r"(&ref));
      }
    });
    auto result = perf.throughputLoop(
        [&] {
          const auto& ref = bank.active();
          asm volatile("" ::"r"(&ref));
        },
        "scaling-320B");
    std::printf("%-12d %-15.3f %-15.0f\n", 320, result.stats.median * 1000.0,
                result.callsPerSecond);
  }
}
