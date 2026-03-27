#ifndef APEX_SYSTEM_CORE_MONTE_CARLO_DRIVER_HPP
#define APEX_SYSTEM_CORE_MONTE_CARLO_DRIVER_HPP
/**
 * @file MonteCarloDriver.hpp
 * @brief Batch Monte Carlo orchestrator for throughput-focused simulation.
 *
 * Design:
 *   - Executes N independent simulation runs across a ThreadPoolLockFree
 *   - Each pool worker grabs runs from an atomic counter (work-stealing)
 *   - No scheduling intelligence, no frame boundaries, pure throughput
 *   - User provides a callable that maps (params, runIndex) -> result
 *   - Results written to pre-allocated vector (no locks during execution)
 *
 * Architecture:
 *   - NOT an executive variant: does not implement IExecutive
 *   - Drives LiteExecutive instances (or raw model calls) as workers
 *   - Parallelism is across runs, not within a single frame
 *   - GPU batching is orthogonal (user handles in their run function)
 *
 * Thread model:
 *   - Pool workers created once at construction, reused across execute() calls
 *   - Internal dispatch via DelegateU8 (zero-allocation callable)
 *   - Completion detection via Latch (one-shot countdown)
 *   - Lock-free task submission (~0.15us per enqueue)
 *
 * Dependencies:
 *   - utilities_concurrency: ThreadPoolLockFree, Latch, DelegateU8
 *
 * Usage:
 * @code
 * // Define parameter and result types
 * struct MyParams { double resistance; double capacitance; };
 * struct MyResult { double peakVoltage; bool converged; };
 *
 * // Generate parameter sweep
 * auto params = apex::monte_carlo::generateSweep<MyParams>(
 *     baseConfig, 10000,
 *     [](MyParams& p, std::uint32_t, std::mt19937_64& rng) {
 *         p.resistance = std::normal_distribution<>(100.0, 5.0)(rng);
 *     });
 *
 * // Run Monte Carlo (pool created once, reused)
 * apex::monte_carlo::MonteCarloDriver<MyParams, MyResult> driver(
 *     [](const MyParams& p, std::uint32_t) -> MyResult {
 *         return {computePeak(p), true};
 *     });
 *
 * auto results = driver.execute(params);
 * @endcode
 *
 * @note NOT RT-safe: Allocates, uses thread pool, blocks on latch.
 */

#include "src/system/core/monte_carlo/inc/MonteCarloResults.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"
#include "src/utilities/concurrency/inc/Latch.hpp"
#include "src/utilities/concurrency/inc/ThreadPoolLockFree.hpp"

#include <cstdint>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <span>
#include <thread>
#include <vector>

namespace apex {
namespace monte_carlo {

/* ----------------------------- Constants ----------------------------- */

/// Use all available hardware threads.
constexpr std::uint32_t AUTO_THREAD_COUNT = 0;

/* ----------------------------- DriverConfig ----------------------------- */

/**
 * @struct DriverConfig
 * @brief Configuration for MonteCarloDriver execution.
 */
struct DriverConfig {
  /// Worker thread count (0 = hardware_concurrency).
  std::uint32_t threadCount{AUTO_THREAD_COUNT};

  /// Per-thread RNG seed offset. Thread i uses (baseSeed + i).
  /// Set to 0 for reproducible default seeding.
  std::uint64_t baseSeed{0};
};

/* ----------------------------- MonteCarloDriver ----------------------------- */

/**
 * @class MonteCarloDriver
 * @brief Executes a batch of independent simulation runs across a thread pool.
 *
 * @tparam ParamT  Parameter type for each run (must be copy-constructible).
 * @tparam ResultT Result type from each run (must be default + move constructible).
 *
 * The driver owns a ThreadPoolLockFree that persists across execute() calls.
 * Pool workers are created once at construction and reused for all sweeps.
 * Internal dispatch uses DelegateU8 (zero-allocation) with Latch for
 * completion detection.
 *
 * The user-provided run function uses std::function for ergonomic lambda
 * support at the API boundary. The internal pool dispatch path is
 * allocation-free (DelegateU8 + void* context).
 *
 * Thread safety:
 *   - execute() must not be called concurrently from multiple threads.
 *   - The run function is called concurrently from pool workers.
 *   - The run function must be thread-safe (no shared mutable state).
 *
 * @note NOT RT-safe: Allocates vectors, uses thread pool.
 */
template <typename ParamT, typename ResultT> class MonteCarloDriver {
  static_assert(std::is_copy_constructible_v<ParamT>, "ParamT must be copy-constructible");
  static_assert(std::is_default_constructible_v<ResultT>, "ResultT must be default-constructible");
  static_assert(std::is_move_constructible_v<ResultT>, "ResultT must be move-constructible");

public:
  /// Run function signature: (params, runIndex) -> result.
  using RunFn = std::function<ResultT(const ParamT&, std::uint32_t)>;

  /**
   * @brief Construct driver with a run function and thread pool.
   * @param runFn  Callable that executes a single simulation run.
   * @param config Driver configuration (thread count, seeding).
   *
   * Creates a ThreadPoolLockFree with the configured thread count.
   * The pool persists for the driver's lifetime, reused across execute().
   *
   * @note NOT RT-safe: Spawns worker threads.
   */
  explicit MonteCarloDriver(RunFn runFn, DriverConfig config = {})
      : runFn_(std::move(runFn)), config_(config),
        pool_(std::make_unique<concurrency::ThreadPoolLockFree>(resolveThreadCount(config),
                                                                resolveThreadCount(config))) {}

  ~MonteCarloDriver() = default;

  // Non-copyable (owns pool), movable
  MonteCarloDriver(const MonteCarloDriver&) = delete;
  MonteCarloDriver& operator=(const MonteCarloDriver&) = delete;
  MonteCarloDriver(MonteCarloDriver&&) noexcept = default;
  MonteCarloDriver& operator=(MonteCarloDriver&&) noexcept = default;

  /* ----------------------------- API ----------------------------- */

  /**
   * @brief Execute all runs and return aggregate results.
   * @param params Span of parameter sets (one per run).
   * @return MonteCarloResults containing per-run results and timing.
   *
   * Submits one worker task per pool thread via DelegateU8. Each worker
   * loops pulling runs from an atomic counter until all runs are consumed.
   * A Latch signals completion when all workers finish.
   *
   * The results vector is pre-allocated to params.size() before
   * workers start. Each worker writes to its own slot (no contention).
   *
   * @note NOT RT-safe: Allocates, blocks on latch.
   */
  [[nodiscard]] MonteCarloResults<ResultT> execute(std::span<const ParamT> params) {
    const auto TOTAL_RUNS = static_cast<std::uint32_t>(params.size());
    if (TOTAL_RUNS == 0) {
      return {};
    }

    const auto WORKER_COUNT = static_cast<std::uint32_t>(pool_->workerCount());

    // Pre-allocate results
    MonteCarloResults<ResultT> results;
    results.runs.resize(TOTAL_RUNS);
    results.totalRuns = TOTAL_RUNS;

    // Shared worker state
    std::atomic<std::uint32_t> nextRun{0};
    std::atomic<std::uint32_t> failedCount{0};
    concurrency::Latch latch(WORKER_COUNT);

    // Pack context for DelegateU8 dispatch
    WorkerCtx ctx{&runFn_,      params.data(), results.runs.data(), TOTAL_RUNS, &nextRun,
                  &failedCount, &latch};

    // Time the execution
    const auto START = std::chrono::steady_clock::now();

    // Submit one work-stealing task per pool worker
    for (std::uint32_t t = 0; t < WORKER_COUNT; ++t) {
      const concurrency::DelegateU8 TASK{workerFn, &ctx};
      pool_->enqueueTask("mc-worker", TASK);
    }

    // Block until all workers drain the run queue
    latch.wait();

    const auto END = std::chrono::steady_clock::now();

    results.wallTimeSeconds = std::chrono::duration<double>(END - START).count();
    results.completedRuns = TOTAL_RUNS - failedCount.load();
    results.failedRuns = failedCount.load();
    results.threadCount = WORKER_COUNT;

    return results;
  }

  /**
   * @brief Execute a single run (no threading).
   * @param params Parameter set for the run.
   * @param runIndex Run index (for seeding/logging).
   * @return Result from the run function.
   *
   * Useful for debugging a specific parameter set.
   *
   * @note NOT RT-safe: Depends on run function.
   */
  [[nodiscard]] ResultT executeSingle(const ParamT& params, std::uint32_t runIndex = 0) const {
    return runFn_(params, runIndex);
  }

  /* ----------------------------- Accessors ----------------------------- */

  /**
   * @brief Get driver configuration.
   * @return Const reference to config.
   */
  [[nodiscard]] const DriverConfig& config() const noexcept { return config_; }

  /**
   * @brief Get pool worker count.
   * @return Number of worker threads in the pool.
   */
  [[nodiscard]] std::uint32_t workerCount() const noexcept {
    return static_cast<std::uint32_t>(pool_->workerCount());
  }

private:
  /* ----------------------------- WorkerCtx ----------------------------- */

  /**
   * @struct WorkerCtx
   * @brief Shared context passed to pool workers via DelegateU8 void* ctx.
   *
   * All fields are either const or atomic. Safe to share across workers.
   */
  struct WorkerCtx {
    const RunFn* runFn;
    const ParamT* params;
    ResultT* results;
    std::uint32_t totalRuns;
    std::atomic<std::uint32_t>* nextRun;
    std::atomic<std::uint32_t>* failedCount;
    concurrency::Latch* latch;
  };

  /* ----------------------------- File Helpers ----------------------------- */

  /**
   * @brief Pool worker function dispatched via DelegateU8.
   * @param raw Pointer to WorkerCtx.
   * @return 0 on success.
   *
   * Loops pulling run indices from the atomic counter until exhausted.
   * Each run writes to its own result slot (no contention).
   * Counts down the latch on exit.
   */
  static std::uint8_t workerFn(void* raw) noexcept {
    auto* ctx = static_cast<WorkerCtx*>(raw);

    while (true) {
      const std::uint32_t RUN_IDX = ctx->nextRun->fetch_add(1, std::memory_order_relaxed);
      if (RUN_IDX >= ctx->totalRuns) {
        break;
      }

      try {
        ctx->results[RUN_IDX] = (*ctx->runFn)(ctx->params[RUN_IDX], RUN_IDX);
      } catch (...) {
        ctx->failedCount->fetch_add(1, std::memory_order_relaxed);
        ctx->results[RUN_IDX] = ResultT{};
      }
    }

    ctx->latch->countDown();
    return 0;
  }

  /**
   * @brief Resolve thread count from config.
   * @param config Driver configuration.
   * @return Actual thread count.
   */
  [[nodiscard]] static std::uint32_t resolveThreadCount(const DriverConfig& config) noexcept {
    std::uint32_t count = config.threadCount;
    if (count == AUTO_THREAD_COUNT) {
      count = std::thread::hardware_concurrency();
      if (count == 0) {
        count = 1;
      }
    }
    return count;
  }

  RunFn runFn_;
  DriverConfig config_;
  std::unique_ptr<concurrency::ThreadPoolLockFree> pool_;
};

} // namespace monte_carlo
} // namespace apex

#endif // APEX_SYSTEM_CORE_MONTE_CARLO_DRIVER_HPP
