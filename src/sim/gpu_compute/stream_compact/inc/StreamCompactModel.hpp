#ifndef APEX_SIM_GPU_COMPUTE_STREAM_COMPACT_MODEL_HPP
#define APEX_SIM_GPU_COMPUTE_STREAM_COMPACT_MODEL_HPP
/**
 * @file StreamCompactModel.hpp
 * @brief GPU stream compaction + classification model with async kick/poll.
 *
 * Simulates a detection pipeline:
 *   1. Generate synthetic sensor field with injected features
 *   2. Threshold: select elements above threshold (stream compaction)
 *   3. Classify: bin selected elements into histogram
 *
 * @note RT-safe: kick/poll are non-blocking on the CPU side.
 */

#include "src/sim/gpu_compute/stream_compact/inc/StreamCompactData.hpp"
#include "src/sim/gpu_compute/stream_compact/inc/StreamCompactKernel.cuh"
#include "src/system/core/infrastructure/system_component/posix/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_blas.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_error.hpp"
#include "src/utilities/helpers/inc/Files.hpp"

#include <cmath>
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

/* ----------------------------- StreamCompactModel ----------------------------- */

/**
 * @class StreamCompactModel
 * @brief GPU stream compaction + classification model with async kick/poll execution.
 *
 * @note RT-safe: kick() and poll() never block on GPU synchronization.
 */
class StreamCompactModel final : public SwModelBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  static constexpr std::uint16_t COMPONENT_ID = 133;
  static constexpr const char* COMPONENT_NAME = "StreamCompactModel";

  /** @brief Get component type identifier. @note RT-safe. */
  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }

  /** @brief Get component name. @note RT-safe. */
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }

  /* ----------------------------- Task UIDs ----------------------------- */

  enum class TaskUid : std::uint8_t { KICK = 1, POLL = 2 };

  /* ----------------------------- Construction ----------------------------- */

  /** @brief Default constructor. */
  StreamCompactModel() noexcept = default;

  /** @brief Destructor releases GPU resources. @note NOT RT-safe. */
  ~StreamCompactModel() override { deallocateGpu(); }

  /* ----------------------------- Lifecycle ----------------------------- */

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;

    registerTask<StreamCompactModel, &StreamCompactModel::kick>(
        static_cast<std::uint8_t>(TaskUid::KICK), this, "compactKick");
    registerTask<StreamCompactModel, &StreamCompactModel::poll>(
        static_cast<std::uint8_t>(TaskUid::POLL), this, "compactPoll");

    registerData(DataCategory::TUNABLE_PARAM, "tunableParams", &tunableParams_.get(),
                 sizeof(StreamCompactTunableParams));
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(StreamCompactState));

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
   * @note RT-SAFE (non-blocking).
   */
  std::uint8_t kick() noexcept {
    auto& s = state_.get();
    ++s.kickCount;

#if COMPAT_HAVE_CUDA
    if (!gpuReady_) {
      return 0;
    }

    if (gpuInFlight_) {
      cudaError_t status = cudaEventQuery(completionEvent_);
      if (status == cudaErrorNotReady) {
        ++s.busyCount;
        return 0;
      }
      harvestResults();
      gpuInFlight_ = false;
    }

    generateInput();

    const auto& p = tunableParams_.get();
    const std::uint32_t N = p.fieldWidth * p.fieldHeight;
    cudaMemcpyAsync(dInput_, hInput_.data(), N * sizeof(float), cudaMemcpyHostToDevice, stream_);

    // Phase 1: Compact
    if (!cuda::streamCompactCuda(dInput_, N, p.threshold, dCompacted_, dCount_, stream_)) {
      ++s.errorCount;
      return 0;
    }

    // Phase 2: Classify (uses count from compact - must sync count first)
    // We chain a classify kernel that reads from dCompacted_ and dCount_.
    // Since classify needs the count, we use a fixed upper bound and let
    // the kernel early-return for threads beyond actual count.
    // For correctness, we'll read count after sync in harvestResults.
    if (!cuda::classifyHistogramCuda(dCompacted_, N, p.classCount, p.classMin, p.classMax,
                                     dHistogram_, stream_)) {
      ++s.errorCount;
      return 0;
    }

    cudaEventRecord(completionEvent_, stream_);
    gpuInFlight_ = true;
    kickTime_ = std::chrono::steady_clock::now();
#endif

    return 0;
  }

  /**
   * @brief Poll: non-blocking GPU completion check.
   * @return 0 always.
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
  [[nodiscard]] const char* label() const noexcept override { return "STREAM_COMPACT"; }

  /* ----------------------------- Accessors ----------------------------- */

  /** @brief Get tunable parameters (read-only). @note RT-safe. */
  [[nodiscard]] const StreamCompactTunableParams& tunableParams() const noexcept {
    return tunableParams_.get();
  }

  /** @brief Get model state (read-only). @note RT-safe. */
  [[nodiscard]] const StreamCompactState& state() const noexcept { return state_.get(); }

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
      return false;
    }

    std::string error;
    StreamCompactTunableParams loaded{};
    if (apex::helpers::files::hex2cpp(tprmPath.string(), loaded, error)) {
      tunableParams_.get() = loaded;
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
    const std::uint32_t N = p.fieldWidth * p.fieldHeight;

    hInput_.resize(N, 0.0f);

    if (cudaMalloc(&dInput_, N * sizeof(float)) != cudaSuccess) {
      return false;
    }
    if (cudaMalloc(&dCompacted_, N * sizeof(float)) != cudaSuccess) {
      deallocateGpu();
      return false;
    }
    if (cudaMalloc(&dCount_, sizeof(std::uint32_t)) != cudaSuccess) {
      deallocateGpu();
      return false;
    }
    if (cudaMalloc(&dHistogram_, p.classCount * sizeof(std::uint32_t)) != cudaSuccess) {
      deallocateGpu();
      return false;
    }

    hHistogram_.resize(p.classCount, 0);

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
    if (dCompacted_ != nullptr) {
      cudaFree(dCompacted_);
      dCompacted_ = nullptr;
    }
    if (dCount_ != nullptr) {
      cudaFree(dCount_);
      dCount_ = nullptr;
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
    const auto& p = tunableParams_.get();
    const auto& s = state_.get();
    const auto SEED = static_cast<float>(s.kickCount);
    const std::uint32_t N = p.fieldWidth * p.fieldHeight;

    for (std::uint32_t i = 0; i < N; ++i) {
      const auto FI = static_cast<float>(i);
      // Noise floor ~0.3 with occasional spikes above threshold
      hInput_[i] = 0.3f + 0.15f * sinf(FI * 0.0013f + SEED * 0.5f) +
                   0.6f * (sinf(FI * 0.00007f + SEED) > 0.7f ? 1.0f : 0.0f);
    }
  }

  void harvestResults() noexcept {
    auto& s = state_.get();
    const auto& p = tunableParams_.get();

    // Read compacted count
    std::uint32_t count = 0;
    cudaMemcpy(&count, dCount_, sizeof(std::uint32_t), cudaMemcpyDeviceToHost);

    s.lastCompactedCount = count;
    s.lastTotalCount = p.fieldWidth * p.fieldHeight;
    s.lastSelectivity = (s.lastTotalCount > 0)
                            ? static_cast<float>(count) / static_cast<float>(s.lastTotalCount)
                            : 0.0f;

    auto now = std::chrono::steady_clock::now();
    s.lastDurationMs = std::chrono::duration<float, std::milli>(now - kickTime_).count();
    ++s.completeCount;

    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(), fmt::format("GPU complete: compacted={}/{} selectivity={:.1f}% "
                                     "duration={:.1f}ms",
                                     s.lastCompactedCount, s.lastTotalCount,
                                     s.lastSelectivity * 100.0f, s.lastDurationMs));
    }
  }
#endif // COMPAT_HAVE_CUDA

  /* ----------------------------- Data ----------------------------- */

  TunableParam<StreamCompactTunableParams> tunableParams_{};
  State<StreamCompactState> state_{};

#if COMPAT_HAVE_CUDA
  std::vector<float> hInput_;
  std::vector<std::uint32_t> hHistogram_;
  float* dInput_{nullptr};
  float* dCompacted_{nullptr};
  std::uint32_t* dCount_{nullptr};
  std::uint32_t* dHistogram_{nullptr};
  cudaStream_t stream_{nullptr};
  cudaEvent_t completionEvent_{nullptr};
  bool gpuReady_{false};
  bool gpuInFlight_{false};
  std::chrono::steady_clock::time_point kickTime_{};
#endif
};

} // namespace gpu_compute
} // namespace sim

#endif // APEX_SIM_GPU_COMPUTE_STREAM_COMPACT_MODEL_HPP
