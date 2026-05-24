/**
 * @file SystemComponent_pTest.cpp
 * @brief Performance tests for SystemComponent<TParams> A/B staging hot paths.
 *
 * Measures:
 *  - activeParams() RT-critical latency (single atomic acquire load)
 *  - load(struct) throughput at small/medium/large param sizes
 *  - apply() hot-reload latency (atomic swap)
 *  - rollback() recovery latency (atomic swap)
 *  - Param size scaling
 *
 * Usage:
 *   ./SystemComponent_PTEST --csv results.csv
 *   ./SystemComponent_PTEST --quick
 *   ./SystemComponent_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SystemComponent.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace ub = vernier::bench;

using system_core::system_component::Status;
using system_core::system_component::SystemComponent;

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

/** @brief Minimal-validation mock component for benchmarking. */
template <typename TParams> class BenchComponent : public SystemComponent<TParams> {
public:
  BenchComponent() noexcept = default;
  [[nodiscard]] std::uint16_t componentId() const noexcept override { return 999; }
  [[nodiscard]] const char* componentName() const noexcept override { return "BenchComponent"; }
  [[nodiscard]] const char* label() const noexcept override { return "BENCH"; }

protected:
  [[nodiscard]] bool validateParams(const TParams&) const noexcept override { return true; }
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    return static_cast<std::uint8_t>(Status::SUCCESS);
  }
};

} // namespace

/* ----------------------------- Active Params Access ----------------------------- */

/**
 * @brief activeParams() latency on small params (RT-critical path).
 */
PERF_TEST(SystemComponentPerf, ActiveParamsSmall) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, perf.config());

  BenchComponent<SmallParams> c;
  SmallParams p{2.0, 1.0, 1, 0};
  (void)c.load(p);
  (void)c.init();

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      const auto& ref = c.activeParams();
      asm volatile("" ::"r"(&ref));
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        const auto& ref = c.activeParams();
        asm volatile("" ::"r"(&ref));
      },
      "activeParams-small");

  std::printf("\n[RT-Critical] activeParams(24B): %.3f ns (%.0f M calls/sec)\n",
              result.stats.median * 1000.0, result.callsPerSecond / 1e6);
}

/**
 * @brief activeParams() latency on medium params.
 */
PERF_TEST(SystemComponentPerf, ActiveParamsMedium) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, perf.config());

  BenchComponent<MediumParams> c;
  MediumParams p{};
  (void)c.load(p);
  (void)c.init();

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      const auto& ref = c.activeParams();
      asm volatile("" ::"r"(&ref));
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        const auto& ref = c.activeParams();
        asm volatile("" ::"r"(&ref));
      },
      "activeParams-medium");

  std::printf("\n[RT-Critical] activeParams(88B): %.3f ns (%.0f M calls/sec)\n",
              result.stats.median * 1000.0, result.callsPerSecond / 1e6);
}

/**
 * @brief activeParams() latency on large params.
 */
PERF_TEST(SystemComponentPerf, ActiveParamsLarge) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, perf.config());

  BenchComponent<LargeParams> c;
  LargeParams p{};
  (void)c.load(p);
  (void)c.init();

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      const auto& ref = c.activeParams();
      asm volatile("" ::"r"(&ref));
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        const auto& ref = c.activeParams();
        asm volatile("" ::"r"(&ref));
      },
      "activeParams-large");

  std::printf("\n[RT-Critical] activeParams(320B): %.3f ns (%.0f M calls/sec)\n",
              result.stats.median * 1000.0, result.callsPerSecond / 1e6);
}

/* ----------------------------- Load ----------------------------- */

/**
 * @brief load(struct) on small params (cold-path memory copy + validation).
 */
PERF_TEST(SystemComponentPerf, LoadSmall) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, perf.config());

  BenchComponent<SmallParams> c;
  SmallParams p{2.0, 1.0, 1, 0};

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      auto status = c.load(p);
      asm volatile("" ::"r"(status));
      (void)status;
    }
  });

  ub::MemoryProfile memProfile{
      .bytesRead = sizeof(SmallParams), .bytesWritten = sizeof(SmallParams), .bytesAllocated = 0};

  auto result = perf.throughputLoop(
      [&] {
        auto status = c.load(p);
        asm volatile("" ::"r"(status));
        (void)status;
      },
      "load-small", memProfile);

  std::printf("\n[Cold-Path] load(24B): %.3f ns (%.0f M calls/sec)\n", result.stats.median * 1000.0,
              result.callsPerSecond / 1e6);
}

/**
 * @brief load(struct) on medium params.
 */
PERF_TEST(SystemComponentPerf, LoadMedium) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, perf.config());

  BenchComponent<MediumParams> c;
  MediumParams p{};

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      auto status = c.load(p);
      asm volatile("" ::"r"(status));
      (void)status;
    }
  });

  ub::MemoryProfile memProfile{
      .bytesRead = sizeof(MediumParams), .bytesWritten = sizeof(MediumParams), .bytesAllocated = 0};

  auto result = perf.throughputLoop(
      [&] {
        auto status = c.load(p);
        asm volatile("" ::"r"(status));
        (void)status;
      },
      "load-medium", memProfile);

  std::printf("\n[Cold-Path] load(88B): %.3f ns (%.0f M calls/sec)\n", result.stats.median * 1000.0,
              result.callsPerSecond / 1e6);
}

/**
 * @brief load(struct) on large params.
 */
PERF_TEST(SystemComponentPerf, LoadLarge) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, perf.config());

  BenchComponent<LargeParams> c;
  LargeParams p{};

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      auto status = c.load(p);
      asm volatile("" ::"r"(status));
      (void)status;
    }
  });

  ub::MemoryProfile memProfile{
      .bytesRead = sizeof(LargeParams), .bytesWritten = sizeof(LargeParams), .bytesAllocated = 0};

  auto result = perf.throughputLoop(
      [&] {
        auto status = c.load(p);
        asm volatile("" ::"r"(status));
        (void)status;
      },
      "load-large", memProfile);

  std::printf("\n[Cold-Path] load(320B): %.3f ns (%.0f M calls/sec)\n",
              result.stats.median * 1000.0, result.callsPerSecond / 1e6);
}

/* ----------------------------- Apply ----------------------------- */

/**
 * @brief apply() hot-reload path (atomic swap).
 */
PERF_TEST(SystemComponentPerf, ApplySmall) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, perf.config());

  BenchComponent<SmallParams> c;
  SmallParams p1{1.0, 0.0, 0, 0};
  SmallParams p2{2.0, 1.0, 1, 0};

  (void)c.load(p1);
  (void)c.init();

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)c.load(p2);
      auto status = c.apply();
      asm volatile("" ::"r"(status));
      (void)status;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        (void)c.load(p2);
        auto status = c.apply();
        asm volatile("" ::"r"(status));
        (void)status;
      },
      "apply-small");

  std::printf("\n[Hot-Reload] load+apply(24B): %.3f ns (%.0f M calls/sec)\n",
              result.stats.median * 1000.0, result.callsPerSecond / 1e6);
}

/* ----------------------------- Rollback ----------------------------- */

/**
 * @brief rollback() recovery path (atomic swap to previous).
 */
PERF_TEST(SystemComponentPerf, RollbackSmall) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, perf.config());

  BenchComponent<SmallParams> c;
  SmallParams p1{1.0, 0.0, 0, 0};
  SmallParams p2{2.0, 1.0, 1, 0};

  (void)c.load(p1);
  (void)c.init();
  (void)c.load(p2);
  (void)c.apply();

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)c.rollback();
      (void)c.load(p2);
      (void)c.apply();
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        (void)c.rollback();
        (void)c.load(p2);
        (void)c.apply();
      },
      "rollback-cycle");

  std::printf("\n[Recovery] rollback+load+apply(24B): %.3f ns (%.0f M calls/sec)\n",
              result.stats.median * 1000.0, result.callsPerSecond / 1e6);
}

/* ----------------------------- Full Lifecycle ----------------------------- */

/**
 * @brief Full init cycle: ctor + load + init + activeParams.
 */
PERF_TEST(SystemComponentPerf, FullInitCycle) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, perf.config());

  SmallParams p{1.0, 0.0, 0, 0};

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      BenchComponent<SmallParams> c;
      (void)c.load(p);
      (void)c.init();
      const auto& ref = c.activeParams();
      asm volatile("" ::"r"(&ref));
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        BenchComponent<SmallParams> c;
        (void)c.load(p);
        (void)c.init();
        const auto& ref = c.activeParams();
        asm volatile("" ::"r"(&ref));
      },
      "full-init-cycle");

  std::printf("\n[Cold-Path] ctor+load+init+activeParams: %.3f ns (%.0f M cycles/sec)\n",
              result.stats.median * 1000.0, result.callsPerSecond / 1e6);
}

/* ----------------------------- Param Size Scaling ----------------------------- */

/**
 * @brief activeParams() across param sizes (24, 88, 320 bytes).
 */
PERF_TEST(SystemComponentPerf, ParamSizeScaling) {
  const ub::PerfConfig CFG = ub::detail::getPerfConfig();

  std::printf("\n[Scaling] activeParams() by param size:\n");
  std::printf("%-12s %-15s %-15s\n", "Size(B)", "Median(ns)", "Calls/sec");
  std::printf("%s\n", std::string(45, '-').c_str());

  {
    ub::PerfCase perf{"SystemComponentPerf.ParamSizeScaling/24B", CFG};
    ub::attachProfilerHooks(perf, CFG);

    BenchComponent<SmallParams> c;
    SmallParams p{1.0, 0.0, 0, 0};
    (void)c.load(p);
    (void)c.init();

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        const auto& ref = c.activeParams();
        asm volatile("" ::"r"(&ref));
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          const auto& ref = c.activeParams();
          asm volatile("" ::"r"(&ref));
        },
        "scale-24B");

    std::printf("%-12d %-15.3f %-15.0f\n", 24, result.stats.median * 1000.0, result.callsPerSecond);
  }

  {
    ub::PerfCase perf{"SystemComponentPerf.ParamSizeScaling/88B", CFG};
    ub::attachProfilerHooks(perf, CFG);

    BenchComponent<MediumParams> c;
    MediumParams p{};
    (void)c.load(p);
    (void)c.init();

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        const auto& ref = c.activeParams();
        asm volatile("" ::"r"(&ref));
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          const auto& ref = c.activeParams();
          asm volatile("" ::"r"(&ref));
        },
        "scale-88B");

    std::printf("%-12d %-15.3f %-15.0f\n", 88, result.stats.median * 1000.0, result.callsPerSecond);
  }

  {
    ub::PerfCase perf{"SystemComponentPerf.ParamSizeScaling/320B", CFG};
    ub::attachProfilerHooks(perf, CFG);

    BenchComponent<LargeParams> c;
    LargeParams p{};
    (void)c.load(p);
    (void)c.init();

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        const auto& ref = c.activeParams();
        asm volatile("" ::"r"(&ref));
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          const auto& ref = c.activeParams();
          asm volatile("" ::"r"(&ref));
        },
        "scale-320B");

    std::printf("%-12d %-15.3f %-15.0f\n", 320, result.stats.median * 1000.0,
                result.callsPerSecond);
  }
}

PERF_MAIN()
