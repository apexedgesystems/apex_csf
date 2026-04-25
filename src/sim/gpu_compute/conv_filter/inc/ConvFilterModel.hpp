#ifndef APEX_SIM_GPU_COMPUTE_CONV_FILTER_MODEL_HPP
#define APEX_SIM_GPU_COMPUTE_CONV_FILTER_MODEL_HPP
/**
 * @file ConvFilterModel.hpp
 * @brief GPU 2D convolution model with async kick/poll execution.
 *
 * Applies a configurable NxN convolution kernel to a synthetic grayscale
 * image on GPU. Demonstrates shared memory tiling, constant memory
 * kernel weights, and the SchedulableTaskCUDA async pattern.
 *
 * Two scheduler tasks:
 *   - KICK (taskUid=1): Check prior completion, harvest results, launch new work.
 *   - POLL (taskUid=2): Non-blocking isComplete() check.
 *
 * @note RT-safe: kick/poll are non-blocking on the CPU side.
 */

#include "src/sim/gpu_compute/conv_filter/inc/ConvFilterData.hpp"
#include "src/sim/gpu_compute/conv_filter/inc/ConvFilterKernel.cuh"
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

/* ----------------------------- ConvFilterModel ----------------------------- */

/**
 * @class ConvFilterModel
 * @brief GPU 2D convolution model with async kick/poll execution.
 *
 * @note RT-safe: kick() and poll() never block on GPU synchronization.
 */
class ConvFilterModel final : public SwModelBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  static constexpr std::uint16_t COMPONENT_ID = 130;
  static constexpr const char* COMPONENT_NAME = "ConvFilterModel";

  /** @brief Get component type identifier. @note RT-safe. */
  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }

  /** @brief Get component name. @note RT-safe. */
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }

  /* ----------------------------- Task UIDs ----------------------------- */

  enum class TaskUid : std::uint8_t { KICK = 1, POLL = 2 };

  /* ----------------------------- Construction ----------------------------- */

  /** @brief Default constructor. */
  ConvFilterModel() noexcept = default;

  /** @brief Destructor releases GPU resources. @note NOT RT-safe. */
  ~ConvFilterModel() override { deallocateGpu(); }

  /* ----------------------------- Lifecycle ----------------------------- */

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;

    registerTask<ConvFilterModel, &ConvFilterModel::kick>(static_cast<std::uint8_t>(TaskUid::KICK),
                                                          this, "convKick");
    registerTask<ConvFilterModel, &ConvFilterModel::poll>(static_cast<std::uint8_t>(TaskUid::POLL),
                                                          this, "convPoll");

    registerData(DataCategory::TUNABLE_PARAM, "tunableParams", &tunableParams_.get(),
                 sizeof(ConvFilterTunableParams));
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(ConvFilterState));

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
   * @brief Kick: check prior completion, harvest results, launch new convolution.
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
    const std::size_t IMG_BYTES = p.imageWidth * p.imageHeight * sizeof(float);
    cudaMemcpyAsync(dInput_, hInput_.data(), IMG_BYTES, cudaMemcpyHostToDevice, stream_);

    // Use separable 2-pass for Gaussian (type 0) and Box (type 3)
    if (p.kernelType == 0 || p.kernelType == 3) {
      if (!cuda::conv2dSeparableCuda(dInput_, p.imageWidth, p.imageHeight, p.kernelRadius, dTemp_,
                                     dOutput_, stream_)) {
        ++s.errorCount;
        return 0;
      }
    } else if (!cuda::conv2dCuda(dInput_, p.imageWidth, p.imageHeight, p.kernelRadius, dOutput_,
                                 stream_)) {
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
  [[nodiscard]] const char* label() const noexcept override { return "CONV_FILTER"; }

  /* ----------------------------- Accessors ----------------------------- */

  /** @brief Get tunable parameters (read-only). @note RT-safe. */
  [[nodiscard]] const ConvFilterTunableParams& tunableParams() const noexcept {
    return tunableParams_.get();
  }

  /** @brief Get model state (read-only). @note RT-safe. */
  [[nodiscard]] const ConvFilterState& state() const noexcept { return state_.get(); }

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
    ConvFilterTunableParams loaded{};
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
    const std::size_t IMG_SIZE = static_cast<std::size_t>(p.imageWidth) * p.imageHeight;
    const std::size_t IMG_BYTES = IMG_SIZE * sizeof(float);

    hInput_.resize(IMG_SIZE, 0.0f);
    hOutput_.resize(IMG_SIZE, 0.0f);

    if (cudaMalloc(&dInput_, IMG_BYTES) != cudaSuccess) {
      return false;
    }
    if (cudaMalloc(&dOutput_, IMG_BYTES) != cudaSuccess) {
      cudaFree(dInput_);
      dInput_ = nullptr;
      return false;
    }
    if (cudaMalloc(&dTemp_, IMG_BYTES) != cudaSuccess) {
      deallocateGpu();
      return false;
    }

    if (cudaStreamCreate(&stream_) != cudaSuccess) {
      deallocateGpu();
      return false;
    }
    if (cudaEventCreateWithFlags(&completionEvent_, cudaEventDisableTiming) != cudaSuccess) {
      deallocateGpu();
      return false;
    }

    // Generate and upload convolution kernel weights
    const std::uint32_t DIAM = 2 * p.kernelRadius + 1;
    std::vector<float> hKernel(DIAM * DIAM);

    if (p.kernelType == 3) {
      cuda::generateBoxKernel(hKernel.data(), p.kernelRadius);
    } else {
      cuda::generateGaussianKernel(hKernel.data(), p.kernelRadius, p.gaussianSigma);
    }

    if (!cuda::convSetKernel(hKernel.data(), p.kernelRadius)) {
      deallocateGpu();
      return false;
    }

    // Upload 1D separable weights (for Gaussian/box kernels)
    std::vector<float> hKernel1D(DIAM);
    if (p.kernelType == 3) {
      const float VAL = 1.0f / static_cast<float>(DIAM);
      for (std::uint32_t i = 0; i < DIAM; ++i) {
        hKernel1D[i] = VAL;
      }
    } else {
      cuda::generateGaussianKernel1D(hKernel1D.data(), p.kernelRadius, p.gaussianSigma);
    }
    if (!cuda::convSetKernel1D(hKernel1D.data(), p.kernelRadius)) {
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
    if (dOutput_ != nullptr) {
      cudaFree(dOutput_);
      dOutput_ = nullptr;
    }
    if (dTemp_ != nullptr) {
      cudaFree(dTemp_);
      dTemp_ = nullptr;
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
    const auto W = static_cast<float>(p.imageWidth);
    const auto H = static_cast<float>(p.imageHeight);

    for (std::uint32_t y = 0; y < p.imageHeight; ++y) {
      for (std::uint32_t x = 0; x < p.imageWidth; ++x) {
        const auto FX = static_cast<float>(x) / W;
        const auto FY = static_cast<float>(y) / H;
        // Synthetic image: sine gratings + noise-like pattern
        hInput_[y * p.imageWidth + x] =
            0.5f + 0.3f * sinf(FX * 20.0f + SEED * 0.2f) * cosf(FY * 15.0f + SEED * 0.1f) +
            0.1f * sinf((FX + FY) * 50.0f + SEED);
      }
    }
  }

  void harvestResults() noexcept {
    const auto& p = tunableParams_.get();
    const std::size_t IMG_SIZE = static_cast<std::size_t>(p.imageWidth) * p.imageHeight;

    cudaMemcpy(hOutput_.data(), dOutput_, IMG_SIZE * sizeof(float), cudaMemcpyDeviceToHost);

    // Compute output statistics (sample every 1024th pixel for speed)
    auto& s = state_.get();
    float minV = hOutput_[0];
    float maxV = hOutput_[0];
    float sum = 0.0f;
    std::uint32_t count = 0;

    for (std::size_t i = 0; i < IMG_SIZE; i += 1024) {
      const float V = hOutput_[i];
      if (V < minV) {
        minV = V;
      }
      if (V > maxV) {
        maxV = V;
      }
      sum += V;
      ++count;
    }

    s.lastOutputMin = minV;
    s.lastOutputMax = maxV;
    s.lastOutputMean = (count > 0) ? sum / static_cast<float>(count) : 0.0f;

    auto now = std::chrono::steady_clock::now();
    s.lastDurationMs = std::chrono::duration<float, std::milli>(now - kickTime_).count();
    ++s.completeCount;

    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(),
                fmt::format("GPU complete: min={:.3f} max={:.3f} mean={:.3f} duration={:.1f}ms "
                            "img={}x{} R={}",
                            s.lastOutputMin, s.lastOutputMax, s.lastOutputMean, s.lastDurationMs,
                            p.imageWidth, p.imageHeight, p.kernelRadius));
    }
  }
#endif // COMPAT_HAVE_CUDA

  /* ----------------------------- Data ----------------------------- */

  TunableParam<ConvFilterTunableParams> tunableParams_{};
  State<ConvFilterState> state_{};

#if COMPAT_HAVE_CUDA
  std::vector<float> hInput_;
  std::vector<float> hOutput_;
  float* dInput_{nullptr};
  float* dOutput_{nullptr};
  float* dTemp_{nullptr};
  cudaStream_t stream_{nullptr};
  cudaEvent_t completionEvent_{nullptr};
  bool gpuReady_{false};
  bool gpuInFlight_{false};
  std::chrono::steady_clock::time_point kickTime_{};
#endif
};

} // namespace gpu_compute
} // namespace sim

#endif // APEX_SIM_GPU_COMPUTE_CONV_FILTER_MODEL_HPP
