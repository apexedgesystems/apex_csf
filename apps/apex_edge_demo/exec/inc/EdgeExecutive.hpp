#ifndef APEX_EDGE_DEMO_EXECUTIVE_HPP
#define APEX_EDGE_DEMO_EXECUTIVE_HPP
/**
 * @file EdgeExecutive.hpp
 * @brief Edge compute demonstration executive with GPU workload models.
 *
 * EdgeExecutive extends ApexExecutive with GPU compute models that
 * demonstrate heavy CUDA workloads running alongside a deterministic
 * real-time scheduler on NVIDIA Jetson AGX Thor.
 *
 * Components:
 *   - ConvFilterModel (SW_MODEL): 2D image convolution on GPU
 *   - BatchStatsModel (SW_MODEL): Parallel reduction on GPU
 *   - StreamCompactModel (SW_MODEL): Threshold + compaction + classify on GPU
 *   - SystemMonitor (SUPPORT): CPU + GPU health telemetry via NVML
 */

#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/support/system_monitor/inc/SystemMonitor.hpp"

#include "src/sim/gpu_compute/batch_stats/inc/BatchStatsModel.hpp"
#include "src/sim/gpu_compute/conv_filter/inc/ConvFilterModel.hpp"
#include "src/sim/gpu_compute/fft_analyzer/inc/FFTAnalyzerModel.hpp"
#include "src/sim/gpu_compute/stream_compact/inc/StreamCompactModel.hpp"

namespace appsim {
namespace exec {

/* ----------------------------- EdgeExecutive ----------------------------- */

/**
 * @class EdgeExecutive
 * @brief Application executive for edge compute GPU demonstration.
 *
 * Registers GPU workload models and system monitor. All configuration
 * (thread affinity, pool layout, task frequencies) is TPRM-driven.
 * Same binary deploys to different hardware with different TPRM files.
 */
class EdgeExecutive : public executive::ApexExecutive {
public:
  using ApexExecutive::ApexExecutive;

  ~EdgeExecutive() override = default;

  [[nodiscard]] const char* label() const noexcept override { return "EDGE_EXECUTIVE"; }

protected:
  /**
   * @brief Register all edge compute application components.
   *
   * Registers:
   *   - 1x ConvFilterModel (SW_MODEL, fullUid=0x8200)
   *   - 1x FFTAnalyzerModel (SW_MODEL, fullUid=0x8300)
   *   - 1x BatchStatsModel (SW_MODEL, fullUid=0x8400)
   *   - 1x StreamCompactModel (SW_MODEL, fullUid=0x8500)
   *   - 1x SystemMonitor (SUPPORT, fullUid=0xC800)
   *
   * @return true on success, false on registration failure.
   */
  [[nodiscard]] bool registerComponents() noexcept override;

private:
  /* ----------------------------- Components ----------------------------- */

  sim::gpu_compute::ConvFilterModel convFilter_;
  sim::gpu_compute::FFTAnalyzerModel fftAnalyzer_;
  sim::gpu_compute::BatchStatsModel batchStats_;
  sim::gpu_compute::StreamCompactModel streamCompact_;
  system_core::support::SystemMonitor sysMonitor_;
};

} // namespace exec
} // namespace appsim

#endif // APEX_EDGE_DEMO_EXECUTIVE_HPP
