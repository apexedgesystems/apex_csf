#ifndef APEX_SIM_GPU_COMPUTE_FFT_ANALYZER_MODEL_HPP
#define APEX_SIM_GPU_COMPUTE_FFT_ANALYZER_MODEL_HPP
/**
 * @file FFTAnalyzerModel.hpp
 * @brief GPU batched FFT analyzer model with async kick/poll execution.
 *
 * Performs batched 1D R2C FFT via cuFFT on multiple sensor channels, then
 * runs a custom kernel for magnitude spectrum computation and per-channel
 * peak detection. Demonstrates cuFFT integration with the async task pattern.
 *
 * @note RT-safe: kick/poll are non-blocking on the CPU side.
 */

#include "src/sim/gpu_compute/fft_analyzer/inc/FFTAnalyzerData.hpp"
#include "src/sim/gpu_compute/fft_analyzer/inc/FFTAnalyzerKernel.cuh"
#include "src/system/core/infrastructure/data/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/apex/inc/SwModelBase.hpp"
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
#include <cufft.h>
#endif

namespace sim {
namespace gpu_compute {

using system_core::data::State;
using system_core::data::TunableParam;
using system_core::system_component::Status;
using system_core::system_component::SwModelBase;

/* ----------------------------- FFTAnalyzerModel ----------------------------- */

/**
 * @class FFTAnalyzerModel
 * @brief GPU batched FFT analyzer model with async kick/poll execution.
 *
 * @note RT-safe: kick() and poll() never block on GPU synchronization.
 */
class FFTAnalyzerModel final : public SwModelBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  static constexpr std::uint16_t COMPONENT_ID = 131;
  static constexpr const char* COMPONENT_NAME = "FFTAnalyzerModel";

  /** @brief Get component type identifier. @note RT-safe. */
  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }

  /** @brief Get component name. @note RT-safe. */
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }

  /* ----------------------------- Task UIDs ----------------------------- */

  enum class TaskUid : std::uint8_t { KICK = 1, POLL = 2 };

  /* ----------------------------- Construction ----------------------------- */

  /** @brief Default constructor. */
  FFTAnalyzerModel() noexcept = default;

  /** @brief Destructor releases GPU resources. @note NOT RT-safe. */
  ~FFTAnalyzerModel() override { deallocateGpu(); }

  /* ----------------------------- Lifecycle ----------------------------- */

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;

    registerTask<FFTAnalyzerModel, &FFTAnalyzerModel::kick>(
        static_cast<std::uint8_t>(TaskUid::KICK), this, "fftKick");
    registerTask<FFTAnalyzerModel, &FFTAnalyzerModel::poll>(
        static_cast<std::uint8_t>(TaskUid::POLL), this, "fftPoll");

    registerData(DataCategory::TUNABLE_PARAM, "tunableParams", &tunableParams_.get(),
                 sizeof(FFTAnalyzerTunableParams));
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(FFTAnalyzerState));

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
    const std::size_t INPUT_BYTES =
        static_cast<std::size_t>(p.channelCount) * p.samplesPerChannel * sizeof(float);
    cudaMemcpyAsync(dInput_, hInput_.data(), INPUT_BYTES, cudaMemcpyHostToDevice, stream_);

    // Execute batched R2C FFT
    cufftResult fftResult =
        cufftExecR2C(fftPlan_, dInput_, reinterpret_cast<cufftComplex*>(dComplex_));
    if (fftResult != CUFFT_SUCCESS) {
      ++s.errorCount;
      return 0;
    }

    // Magnitude spectrum + peak detection
    if (!cuda::fftMagnitudePeaksCuda(dComplex_, p.channelCount, p.samplesPerChannel, p.sampleRateHz,
                                     dPeaks_, stream_)) {
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
  [[nodiscard]] const char* label() const noexcept override { return "FFT_ANALYZER"; }

  /* ----------------------------- Accessors ----------------------------- */

  /** @brief Get tunable parameters (read-only). @note RT-safe. */
  [[nodiscard]] const FFTAnalyzerTunableParams& tunableParams() const noexcept {
    return tunableParams_.get();
  }

  /** @brief Get model state (read-only). @note RT-safe. */
  [[nodiscard]] const FFTAnalyzerState& state() const noexcept { return state_.get(); }

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
    FFTAnalyzerTunableParams loaded{};
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
    const std::size_t INPUT_SIZE = static_cast<std::size_t>(p.channelCount) * p.samplesPerChannel;
    const std::size_t COMPLEX_SIZE =
        static_cast<std::size_t>(p.channelCount) * (p.samplesPerChannel / 2 + 1) * 2;

    hInput_.resize(INPUT_SIZE, 0.0f);
    hPeaks_.resize(p.channelCount);

    if (cudaMalloc(&dInput_, INPUT_SIZE * sizeof(float)) != cudaSuccess) {
      return false;
    }
    if (cudaMalloc(&dComplex_, COMPLEX_SIZE * sizeof(float)) != cudaSuccess) {
      deallocateGpu();
      return false;
    }
    if (cudaMalloc(&dPeaks_, p.channelCount * sizeof(ChannelPeak)) != cudaSuccess) {
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

    // Create batched R2C FFT plan
    int n = static_cast<int>(p.samplesPerChannel);
    cufftResult result =
        cufftPlanMany(&fftPlan_, 1, &n,      // 1D transform of size n
                      nullptr, 1, n,         // Input: stride=1, dist=n (contiguous channels)
                      nullptr, 1, n / 2 + 1, // Output: stride=1, dist=n/2+1
                      CUFFT_R2C,
                      static_cast<int>(p.channelCount) // Batch count
        );

    if (result != CUFFT_SUCCESS) {
      deallocateGpu();
      return false;
    }

    cufftSetStream(fftPlan_, stream_);
    fftPlanCreated_ = true;
    gpuReady_ = true;
    return true;
#endif
  }

  void deallocateGpu() noexcept {
#if COMPAT_HAVE_CUDA
    if (fftPlanCreated_) {
      cufftDestroy(fftPlan_);
      fftPlanCreated_ = false;
    }
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
    if (dComplex_ != nullptr) {
      cudaFree(dComplex_);
      dComplex_ = nullptr;
    }
    if (dPeaks_ != nullptr) {
      cudaFree(dPeaks_);
      dPeaks_ = nullptr;
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
    const float DT = 1.0f / p.sampleRateHz;

    for (std::uint32_t ch = 0; ch < p.channelCount; ++ch) {
      float* chBuf = hInput_.data() + ch * p.samplesPerChannel;
      // Each channel has a distinct dominant frequency + noise
      const float FREQ = 100.0f + static_cast<float>(ch) * 37.5f; // Hz
      const float AMP = 1.0f;
      const float NOISE_AMP = 0.1f;

      for (std::uint32_t i = 0; i < p.samplesPerChannel; ++i) {
        const float T = static_cast<float>(i) * DT;
        // Clean sinusoid + low-level noise
        chBuf[i] = AMP * sinf(2.0f * 3.14159265f * FREQ * T) +
                   NOISE_AMP * sinf(2.0f * 3.14159265f * 7.3f * static_cast<float>(i) +
                                    SEED * 1.7f + static_cast<float>(ch) * 0.3f);
      }
    }
  }

  void harvestResults() noexcept {
    auto& s = state_.get();
    const auto& p = tunableParams_.get();

    cudaMemcpy(hPeaks_.data(), dPeaks_, p.channelCount * sizeof(ChannelPeak),
               cudaMemcpyDeviceToHost);

    // Report channel 0 peak
    if (!hPeaks_.empty()) {
      s.lastPeakFreqHz = hPeaks_[0].peakFreqHz;
      s.lastPeakMagnitudeDb = hPeaks_[0].peakMagnitudeDb;
    }

    auto now = std::chrono::steady_clock::now();
    s.lastDurationMs = std::chrono::duration<float, std::milli>(now - kickTime_).count();
    ++s.completeCount;

    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(), fmt::format("GPU complete: ch0_peak={:.1f}Hz ({:.1f}dB) channels={} "
                                     "duration={:.1f}ms",
                                     s.lastPeakFreqHz, s.lastPeakMagnitudeDb, p.channelCount,
                                     s.lastDurationMs));
    }
  }
#endif // COMPAT_HAVE_CUDA

  /* ----------------------------- Data ----------------------------- */

  TunableParam<FFTAnalyzerTunableParams> tunableParams_{};
  State<FFTAnalyzerState> state_{};

#if COMPAT_HAVE_CUDA
  std::vector<float> hInput_;
  std::vector<ChannelPeak> hPeaks_;
  float* dInput_{nullptr};
  float* dComplex_{nullptr};
  ChannelPeak* dPeaks_{nullptr};
  cudaStream_t stream_{nullptr};
  cudaEvent_t completionEvent_{nullptr};
  cufftHandle fftPlan_{};
  bool fftPlanCreated_{false};
  bool gpuReady_{false};
  bool gpuInFlight_{false};
  std::chrono::steady_clock::time_point kickTime_{};
#endif
};

} // namespace gpu_compute
} // namespace sim

#endif // APEX_SIM_GPU_COMPUTE_FFT_ANALYZER_MODEL_HPP
