/**
 * @file Registry_pTest.cpp
 * @brief Performance tests for the ApexRegistry component lookup paths.
 *
 * Measures:
 *  - Component lookup latency (O(n) scan) at 10 and 50 entries plus miss
 *  - Task lookup latency at 100-task scale
 *  - Data lookup latency at 100-entry scale
 *  - Bulk iteration throughput (components and tasks)
 *  - Component registration overhead
 *  - Status-to-string conversion
 *
 * Usage:
 *   ./Registry_PTEST --csv results.csv
 *   ./Registry_PTEST --quick
 *   ./Registry_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/components/registry/apex/inc/ApexRegistry.hpp"
#include "src/system/core/components/registry/apex/inc/RegistryData.hpp"
#include "src/system/core/components/registry/apex/inc/RegistryStatus.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/DataCategory.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ub = vernier::bench;
using namespace system_core::registry;
using namespace system_core::schedulable;
using system_core::data::DataCategory;

/* ----------------------------- Test Fixtures ----------------------------- */

namespace {

/** @brief No-op task function used by populated-registry fixtures. */
std::uint8_t noopTaskFn(void*) noexcept { return 0; }

/** @brief Pre-populated registry for lookup benchmarks. */
class PopulatedRegistry {
public:
  PopulatedRegistry(std::size_t numComponents, std::size_t tasksPerComponent,
                    std::size_t dataPerComponent)
      : registry_() {
    (void)registry_.init();

    // Register components
    for (std::size_t c = 0; c < numComponents; ++c) {
      std::uint32_t fullUid = static_cast<std::uint32_t>((100 + c) << 8);
      names_.push_back("Component_" + std::to_string(c));
      (void)registry_.registerComponent(fullUid, names_.back().c_str());

      // Register tasks - use delegate-based SchedulableTask
      for (std::size_t t = 0; t < tasksPerComponent; ++t) {
        SchedulableTaskBase::TaskFn fn{&noopTaskFn, nullptr};
        auto task = std::make_unique<SchedulableTask>(fn, "Task");
        (void)registry_.registerTask(fullUid, static_cast<std::uint8_t>(t), "Task", task.get());
        tasks_.push_back(std::move(task));
      }

      // Register data
      for (std::size_t d = 0; d < dataPerComponent; ++d) {
        dataBlocks_.push_back(std::vector<std::uint8_t>(64, 0xAB));
        (void)registry_.registerData(fullUid, DataCategory::STATE, "Data",
                                     dataBlocks_.back().data(), dataBlocks_.back().size());
      }
    }

    (void)registry_.freeze();
  }

  ApexRegistry& registry() { return registry_; }
  std::size_t componentCount() const { return registry_.componentCount(); }

private:
  ApexRegistry registry_;
  std::vector<std::string> names_;
  std::vector<std::unique_ptr<SchedulableTask>> tasks_;
  std::vector<std::vector<std::uint8_t>> dataBlocks_;
};

} // namespace

/* ----------------------------- Component Lookup Benchmarks ----------------------------- */

/**
 * @brief Benchmark component lookup with small registry (10 components).
 */
PERF_TEST(RegistryPerf, ComponentLookup_10) {
  UB_PERF_GUARD(perf);

  PopulatedRegistry pop(10, 2, 2);
  std::uint32_t targetUid = (105 << 8); // Middle of range

  auto fn = [&]() {
    auto* entry = pop.registry().getComponent(targetUid);
    (void)entry;
  };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "ComponentLookup_10");
}

/**
 * @brief Benchmark component lookup with medium registry (50 components).
 */
PERF_TEST(RegistryPerf, ComponentLookup_50) {
  UB_PERF_GUARD(perf);

  PopulatedRegistry pop(50, 2, 2);
  std::uint32_t targetUid = (125 << 8); // Middle of range

  auto fn = [&]() {
    auto* entry = pop.registry().getComponent(targetUid);
    (void)entry;
  };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "ComponentLookup_50");
}

/**
 * @brief Benchmark component lookup miss (not found).
 */
PERF_TEST(RegistryPerf, ComponentLookup_Miss) {
  UB_PERF_GUARD(perf);

  PopulatedRegistry pop(50, 2, 2);
  std::uint32_t badUid = (999 << 8); // Not registered

  auto fn = [&]() {
    auto* entry = pop.registry().getComponent(badUid);
    (void)entry;
  };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "ComponentLookup_Miss");
  // Full scan for miss should still be fast
}

/* ----------------------------- Task Lookup Benchmarks ----------------------------- */

/**
 * @brief Benchmark task lookup.
 */
PERF_TEST(RegistryPerf, TaskLookup) {
  UB_PERF_GUARD(perf);

  PopulatedRegistry pop(20, 5, 2); // 100 tasks total
  std::uint32_t targetUid = (110 << 8);
  std::uint8_t taskUid = 2;

  auto fn = [&]() {
    auto* entry = pop.registry().getTask(targetUid, taskUid);
    (void)entry;
  };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "TaskLookup_100");
}

/* ----------------------------- Data Lookup Benchmarks ----------------------------- */

/**
 * @brief Benchmark data lookup by UID and category.
 */
PERF_TEST(RegistryPerf, DataLookup) {
  UB_PERF_GUARD(perf);

  PopulatedRegistry pop(20, 2, 5); // 100 data entries
  std::uint32_t targetUid = (115 << 8);

  auto fn = [&]() {
    auto* entry = pop.registry().getData(targetUid, DataCategory::STATE);
    (void)entry;
  };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "DataLookup_100");
}

/* ----------------------------- Bulk Iteration Benchmarks ----------------------------- */

/**
 * @brief Benchmark iterating all components.
 */
PERF_TEST(RegistryPerf, IterateAllComponents) {
  UB_PERF_GUARD(perf);

  PopulatedRegistry pop(50, 2, 2);

  auto fn = [&]() {
    auto span = pop.registry().getAllComponents();
    std::size_t count = 0;
    for (const auto& entry : span) {
      (void)entry.fullUid;
      count++;
    }
    volatile std::size_t sink = count;
    (void)sink;
  };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "IterateComponents_50");
}

/**
 * @brief Benchmark iterating all tasks.
 */
PERF_TEST(RegistryPerf, IterateAllTasks) {
  UB_PERF_GUARD(perf);

  PopulatedRegistry pop(20, 10, 2); // 200 tasks

  auto fn = [&]() {
    auto span = pop.registry().getAllTasks();
    std::size_t count = 0;
    for (const auto& entry : span) {
      (void)entry.taskUid;
      count++;
    }
    volatile std::size_t sink = count;
    (void)sink;
  };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "IterateTasks_200");
}

/* ----------------------------- Registration Benchmarks ----------------------------- */

/**
 * @brief Benchmark component registration overhead.
 *
 * Note: This creates fresh registries each iteration as registration
 * mutates state. Measures the setup/teardown overhead.
 */
PERF_TEST(RegistryPerf, RegisterComponents) {
  UB_PERF_GUARD(perf);

  auto fn = [&]() {
    ApexRegistry reg;
    (void)reg.init();
    for (std::size_t i = 0; i < 10; ++i) {
      std::uint32_t uid = static_cast<std::uint32_t>((100 + i) << 8);
      (void)reg.registerComponent(uid, "Component");
    }
    (void)reg.freeze();
  };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "Register_10_Components");
}

/* ----------------------------- Status Conversion Benchmarks ----------------------------- */

/**
 * @brief Benchmark status to string conversion.
 */
PERF_TEST(RegistryPerf, StatusToString) {
  UB_PERF_GUARD(perf);

  std::array<Status, 5> statuses = {Status::SUCCESS, Status::ERROR_COMPONENT_NOT_FOUND,
                                    Status::ERROR_ALREADY_FROZEN, Status::ERROR_DUPLICATE_COMPONENT,
                                    Status::WARN_EMPTY_NAME};
  std::size_t idx = 0;

  auto fn = [&]() {
    const char* str = toString(statuses[idx++ % statuses.size()]);
    (void)str;
  };

  perf.warmup(fn);
  auto result = perf.throughputLoop(fn, "StatusToString");
}

PERF_MAIN()
