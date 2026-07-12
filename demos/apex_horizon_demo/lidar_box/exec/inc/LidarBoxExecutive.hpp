#ifndef APEX_HORIZON_DEMO_LIDAR_BOX_EXECUTIVE_HPP
#define APEX_HORIZON_DEMO_LIDAR_BOX_EXECUTIVE_HPP
/**
 * @file LidarBoxExecutive.hpp
 * @brief Executive for the lidar_box demo.
 *
 * Two components on a 50 Hz clock:
 *   - LidarBoxProducer -- drifts the body, measures the six wall clearances,
 *     publishes the 48-byte LidarBoxFrame OUTPUT block.
 *   - ShmRingBridge -- streams that OUTPUT to the /lidar_box shared-memory ring
 *     (LBOX/v1) for an out-of-process visualizer. Idle-on-failure: with no
 *     consumer attached the sim runs unchanged.
 *
 * Executive responsibilities:
 *   1. Register both components (tunables load from per-component TPRMs).
 *   2. Wire the bridge's source resolver to the registry so its TPRM-selected
 *      (fullUid, category) source resolves to the producer's live frame bytes.
 *   3. Hand control to the scheduler (bodyStep 50 Hz before bridgeStep 50 Hz
 *      by priority; telemetry at 1 Hz).
 */

#include "demos/apex_horizon_demo/lidar_box/producer/inc/LidarBoxProducer.hpp"
#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/support/shm_ring_bridge/inc/ShmRingBridge.hpp"

namespace appsim {
namespace exec {

/* ----------------------------- LidarBoxExecutive ----------------------------- */

class LidarBoxExecutive : public executive::ApexExecutive {
public:
  using ApexExecutive::ApexExecutive;

  ~LidarBoxExecutive() override = default;

  [[nodiscard]] const char* label() const noexcept override { return "LIDAR_BOX_EXEC"; }

protected:
  /// Register the producer + bridge; wire the bridge resolver to the registry.
  [[nodiscard]] bool registerComponents() noexcept override;

  /// Post-registration smoke log: component uids + the bridge channel state.
  void configureComponents() noexcept override;

private:
  /* ----------------------------- Components ----------------------------- */

  appsim::lidar_box::LidarBoxProducer producer_;
  system_core::support::ShmRingBridge bridge_;
};

} // namespace exec
} // namespace appsim

#endif // APEX_HORIZON_DEMO_LIDAR_BOX_EXECUTIVE_HPP
