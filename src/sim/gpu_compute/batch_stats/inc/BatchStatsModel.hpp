#ifndef APEX_SIM_GPU_COMPUTE_BATCH_STATS_MODEL_HPP
#define APEX_SIM_GPU_COMPUTE_BATCH_STATS_MODEL_HPP
/**
 * @file BatchStatsModel.hpp
 * @brief GPU batch statistics model with async kick/poll execution.
 *
 * Computes parallel reduction (min/max/mean/variance) and histogram over
 * large float arrays on GPU. Demonstrates the SchedulableTaskCUDA async
 * pattern: kick() launches work, poll() checks completion.
 *
 * Two scheduler tasks:
 *   - KICK (taskUid=1): Check prior completion, harvest results, launch new work.
 *   - POLL (taskUid=2): Non-blocking isComplete() check + telemetry update.
 *
 * @note RT-safe: kick/poll are non-blocking on the CPU side. GPU work runs
 *       asynchronously on its own stream.
 */

#include "src/sim/gpu_compute/batch_stats/inc/BatchStatsData.hpp"
#include "src/sim/gpu_compute/batch_stats/inc/BatchStatsKernel.cuh"
#include "src/system/core/infrastructure/system_component/apex/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/apex/inc/SwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_blas.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_error.hpp"
#include "src/utilities/helpers/inc/Files.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include <fmt/format.h>

#if COMPAT_HAVE_CUDA
#include <cuda_runtime.h>
#endif

namespace sim {
namespace gpu_compute {

using system_core::data::State;
using system_core::data::TunableParam;
using system_core::system_component::Status;
using system_core::system_component::SwModelBase;

/* ----------------------------- BatchStatsModel ----------------------------- */

/**
 * @class BatchStatsModel
 * @brief GPU parallel reduction model with async kick/poll execution.
 *
 * Allocates device memory at init, launches reduction + histogram kernels
 * on each kick, and polls for completion. Results are harvested on the
 * next kick after GPU work finishes.
 *
 * @note RT-safe: kick() and poll() never block on GPU synchronization.
 */
class BatchStatsModel final : public SwModelBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  static constexpr std::uint16_t COMPONENT_ID = 132;
  static constexpr const char* COMPONENT_NAME = "BatchStatsModel";

  /** @brief Get component type identifier. @note RT-safe. */
  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }

  /** @brief Get component name. @note RT-safe. */
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }

  /* ----------------------------- Task UIDs ----------------------------- */

  enum class TaskUid : std::uint8_t {
    KICK = 1, ///< Launch GPU work (or skip if still running).
    POLL = 2  ///< Non-blocking completion check.
  };

  /* ----------------------------- Construction ----------------------------- */

  /** @brief Default constructor. */
  BatchStatsModel() noexcept = default;

  /** @brief Destructor releases GPU resources. @note NOT RT-safe. */
  ~BatchStatsModel() override { deallocateGpu(); }

  /* ----------------------------- Lifecycle ----------------------------- */

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;

    registerTask<BatchStatsModel, &BatchStatsModel::kick>(static_cast<std::uint8_t>(TaskUid::KICK),
                                                          this, "batchKick");
    registerTask<BatchStatsModel, &BatchStatsModel::poll>(static_cast<std::uint8_t>(TaskUid::POLL),
                                                          this, "batchPoll");

    registerData(DataCategory::TUNABLE_PARAM, "tunableParams", &tunableParams_.get(),
                 sizeof(BatchStatsTunableParams));
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(BatchStatsState));

    if (!allocateGpu()) {
      auto* log = componentLog();
      if (log != nullptr) {
        log->warning(label(), 0, "GPU allocation failed; model will run in stub mode");
      }
    }

    return static_cast<std::uint8_t>(Status::SUCCESS);
  }

public:
  /* ----------------------------- Task Methods ----------------------------- */

  /**
   * @brief Kick: check prior completion, harvest results, launch new GPU work.
   * @return 0 on success.
   *
   * If the prior GPU run is still in flight, increments busyCount and returns.
   * Otherwise harvests results, regenerates input data, and launches kernels.
   *
   * @note RT-SAFE (non-blocking). GPU launch is ~10-50 us on CPU side.
   */
  std::uint8_t kick() noexcept {
    auto& s = state_.get();
    ++s.kickCount;

#if COMPAT_HAVE_CUDA
    if (!gpuReady_) {
      return 0;
    }

    // Check if prior work is done
    if (gpuInFlight_) {
      cudaError_t status = cudaEventQuery(completionEvent_);
      if (status == cudaErrorNotReady) {
        ++s.busyCount;
        return 0;
      }
      // Completed (or error) - harvest results
      harvestResults();
      gpuInFlight_ = false;
    }

    // Generate synthetic input (simple deterministic pattern)
    generateInput();

    // H2D transfer
    const std::size_t INPUT_BYTES = tunableParams_.get().elementCount * sizeof(float);
    cudaMemcpyAsync(dInput_, hInput_.data(), INPUT_BYTES, cudaMemcpyHostToDevice, stream_);

    // Launch stats kernel
    const auto& p = tunableParams_.get();
    if (!cuda::batchStatsCuda(dInput_, p.elementCount, p.groupSize, dStats_, stream_)) {
      ++s.errorCount;
      return 0;
    }

    // Launch histogram kernel
    if (!cuda::batchHistogramCuda(dInput_, p.elementCount, p.groupSize, p.histogramBins,
                                  p.histogramMin, p.histogramMax, dHistogram_, stream_)) {
      ++s.errorCount;
      return 0;
    }

    // Record completion event
    cudaEventRecord(completionEvent_, stream_);
    gpuInFlight_ = true;
    kickTime_ = std::chrono::steady_clock::now();
#endif

    return 0;
  }

  /**
   * @brief Poll: non-blocking GPU completion check.
   * @return 0 always.
   *
   * @note RT-SAFE (~100 ns).
   */
  std::uint8_t poll() noexcept {
#if COMPAT_HAVE_CUDA
    if (!gpuReady_ || !gpuInFlight_) {
      return 0;
    }

    cudaError_t status = cudaEventQuery(completionEvent_);
    if (status == cudaSuccess) {
      harvestResults();
      gpuInFlight_ = false;
    }
#endif
    return 0;
  }

  /** @brief Model label. @note RT-safe. */
  [[nodiscard]] const char* label() const noexcept override { return "BATCH_STATS"; }

  /* ----------------------------- Accessors ----------------------------- */

  /** @brief Get tunable parameters (read-only). @note RT-safe. */
  [[nodiscard]] const BatchStatsTunableParams& tunableParams() const noexcept {
    return tunableParams_.get();
  }

  /** @brief Get model state (read-only). @note RT-safe. */
  [[nodiscard]] const BatchStatsState& state() const noexcept { return state_.get(); }

  /* ----------------------------- TPRM ----------------------------- */

  /** @brief Load tunable parameters from TPRM directory. @note NOT RT-safe: file I/O. */
  bool loadTprm(const std::filesystem::path& tprmDir) noexcept override {
    if (!isRegistered()) {
      return false;
    }

    char filename[32];
    std::snprintf(filename, sizeof(filename), "%06x.tprm", fullUid());
    std::filesystem::path tprmPath = tprmDir / filename;

    if (!std::filesystem::exists(tprmPath)) {
      logTprmConfig("defaults");
      return false;
    }

    std::string error;
    BatchStatsTunableParams loaded{};
    if (apex::helpers::files::hex2cpp(tprmPath.string(), loaded, error)) {
      tunableParams_.get() = loaded;
      logTprmConfig(tprmPath.string());
      return true;
    }
    return false;
  }

private:
  /* ----------------------------- GPU Memory Management ----------------------------- */

  bool allocateGpu() noexcept {
#if !COMPAT_HAVE_CUDA
    return false;
#else
    if (!::apex::compat::cuda::runtimeAvailable()) {
      return false;
    }

    const auto& p = tunableParams_.get();
    const std::uint32_t GROUP_COUNT = (p.elementCount + p.groupSize - 1) / p.groupSize;

    // Host input buffer
    hInput_.resize(p.elementCount, 0.0f);

    // Device allocations
    if (cudaMalloc(&dInput_, p.elementCount * sizeof(float)) != cudaSuccess) {
      return false;
    }
    if (cudaMalloc(&dStats_, GROUP_COUNT * sizeof(GroupStats)) != cudaSuccess) {
      cudaFree(dInput_);
      dInput_ = nullptr;
      return false;
    }
    if (cudaMalloc(&dHistogram_, GROUP_COUNT * p.histogramBins * sizeof(std::uint32_t)) !=
        cudaSuccess) {
      cudaFree(dInput_);
      cudaFree(dStats_);
      dInput_ = nullptr;
      dStats_ = nullptr;
      return false;
    }

    // Host result buffers
    hStats_.resize(GROUP_COUNT);
    hHistogram_.resize(GROUP_COUNT * p.histogramBins, 0);

    // Stream and event
    if (cudaStreamCreate(&stream_) != cudaSuccess) {
      deallocateGpu();
      return false;
    }
    if (cudaEventCreateWithFlags(&completionEvent_, cudaEventDisableTiming) != cudaSuccess) {
      deallocateGpu();
      return false;
    }

    gpuReady_ = true;
    return true;
#endif
  }

  void deallocateGpu() noexcept {
#if COMPAT_HAVE_CUDA
    if (completionEvent_ != nullptr) {
      cudaEventDestroy(completionEvent_);
      completionEvent_ = nullptr;
    }
    if (stream_ != nullptr) {
      cudaStreamDestroy(stream_);
      stream_ = nullptr;
    }
    if (dInput_ != nullptr) {
      cudaFree(dInput_);
      dInput_ = nullptr;
    }
    if (dStats_ != nullptr) {
      cudaFree(dStats_);
      dStats_ = nullptr;
    }
    if (dHistogram_ != nullptr) {
      cudaFree(dHistogram_);
      dHistogram_ = nullptr;
    }
    gpuReady_ = false;
#endif
  }

  /* ----------------------------- Helpers ----------------------------- */

#if COMPAT_HAVE_CUDA
  void generateInput() noexcept {
    // Deterministic synthetic data: sine-wave pattern with drift
    const auto& s = state_.get();
    const std::uint32_t N = tunableParams_.get().elementCount;
    const auto SEED = static_cast<float>(s.kickCount);
    for (std::uint32_t i = 0; i < N; ++i) {
      // Simple LCG-style pseudo-random with spatial structure
      const auto FI = static_cast<float>(i);
      hInput_[i] = 5.0f * sinf(FI * 0.001f + SEED * 0.1f) +
                   2.0f * cosf(FI * 0.0037f + SEED * 0.3f) +
                   0.5f * sinf(FI * 0.017f) * cosf(SEED * 0.7f);
    }
  }

  void harvestResults() noexcept {
    const auto& p = tunableParams_.get();
    const std::uint32_t GROUP_COUNT = (p.elementCount + p.groupSize - 1) / p.groupSize;

    // D2H transfer (blocking but small)
    cudaMemcpy(hStats_.data(), dStats_, GROUP_COUNT * sizeof(GroupStats), cudaMemcpyDeviceToHost);

    // Aggregate across groups for state
    auto& s = state_.get();
    if (!hStats_.empty()) {
      float globalMin = hStats_[0].minVal;
      float globalMax = hStats_[0].maxVal;
      float totalSum = 0.0f;
      std::uint32_t totalCount = 0;

      for (const auto& g : hStats_) {
        if (g.minVal < globalMin) {
          globalMin = g.minVal;
        }
        if (g.maxVal > globalMax) {
          globalMax = g.maxVal;
        }
        totalSum += g.sum;
        totalCount += g.count;
      }

      s.lastMinVal = globalMin;
      s.lastMaxVal = globalMax;
      s.lastMeanVal = (totalCount > 0) ? totalSum / static_cast<float>(totalCount) : 0.0f;

      // Variance: E[X^2] - E[X]^2
      float totalSumSq = 0.0f;
      for (const auto& g : hStats_) {
        totalSumSq += g.sumSq;
      }
      const float MEAN = s.lastMeanVal;
      s.lastVariance =
          (totalCount > 0) ? totalSumSq / static_cast<float>(totalCount) - MEAN * MEAN : 0.0f;
    }

    // Duration
    auto now = std::chrono::steady_clock::now();
    s.lastDurationMs = std::chrono::duration<float, std::milli>(now - kickTime_).count();

    ++s.completeCount;

    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(), fmt::format("GPU complete: min={:.3f} max={:.3f} mean={:.3f} var={:.3f} "
                                     "duration={:.1f}ms count={}",
                                     s.lastMinVal, s.lastMaxVal, s.lastMeanVal, s.lastVariance,
                                     s.lastDurationMs, s.completeCount));
    }
  }
#endif // COMPAT_HAVE_CUDA

  void logTprmConfig(const std::string& source) noexcept {
    auto* log = componentLog();
    if (log == nullptr) {
      return;
    }
    const auto& p = tunableParams_.get();
    log->info(label(), "=== TPRM Configuration ===");
    log->info(label(), fmt::format("Source: {}", source));
    log->info(label(), fmt::format("Elements: {}, GroupSize: {}, HistBins: {}", p.elementCount,
                                   p.groupSize, p.histogramBins));
    log->info(label(), fmt::format("HistRange: [{}, {}]", p.histogramMin, p.histogramMax));
    log->info(label(), "==========================");
  }

  /* ----------------------------- Data ----------------------------- */

  TunableParam<BatchStatsTunableParams> tunableParams_{};
  State<BatchStatsState> state_{};

#if COMPAT_HAVE_CUDA
  // Host buffers
  std::vector<float> hInput_;
  std::vector<GroupStats> hStats_;
  std::vector<std::uint32_t> hHistogram_;

  // Device buffers
  float* dInput_{nullptr};
  GroupStats* dStats_{nullptr};
  std::uint32_t* dHistogram_{nullptr};

  // CUDA resources
  cudaStream_t stream_{nullptr};
  cudaEvent_t completionEvent_{nullptr};

  bool gpuReady_{false};
  bool gpuInFlight_{false};
  std::chrono::steady_clock::time_point kickTime_{};
#endif
};

} // namespace gpu_compute
} // namespace sim

#endif // APEX_SIM_GPU_COMPUTE_BATCH_STATS_MODEL_HPP
