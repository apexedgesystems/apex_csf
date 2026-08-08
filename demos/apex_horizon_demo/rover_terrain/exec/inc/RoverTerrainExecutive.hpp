#ifndef APEX_HORIZON_DEMO_ROVER_TERRAIN_EXECUTIVE_HPP
#define APEX_HORIZON_DEMO_ROVER_TERRAIN_EXECUTIVE_HPP
/**
 * @file RoverTerrainExecutive.hpp
 * @brief Executive for the rover_terrain demo.
 *
 * Composes the world and the rover:
 *   - Two passive CelestialBody components (Earth + Moon), configured
 *     via per-component TPRM tunables.
 *   - A WorldQueryProbe per body, scheduled at 1 Hz as the running
 *     world-sanity check.
 *   - The GroundVehicle driving Earth's terrain at 10 Hz.
 *   - One ShmRingBridge publishing the rover's 256-byte ROVR/1 OUTPUT
 *     to /horizon_rover each 10 Hz bridge tick. The bridge idles if no
 *     consumer attaches; the sim runs unchanged.
 *
 * The executive's C++ specifies cross-component wiring only (probe and
 * vehicle body pointers, the bridge's registry resolver); all
 * configuration values come from the TPRM set.
 */

#include "demos/apex_horizon_demo/ground_vehicle/inc/GroundVehicle.hpp"
#include "demos/apex_horizon_demo/world_query_probe/inc/WorldQueryProbe.hpp"
#include "src/sim/environment/celestial_body/inc/CelestialBody.hpp"
#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/support/shm_ring_bridge/inc/ShmRingBridge.hpp"

namespace appsim {
namespace rover_terrain {

/* ----------------------------- RoverTerrainExecutive ----------------------------- */

class RoverTerrainExecutive : public executive::ApexExecutive {
public:
  using ApexExecutive::ApexExecutive;

  ~RoverTerrainExecutive() override = default;

  [[nodiscard]] const char* label() const noexcept override { return "ROVER_TERRAIN_EXEC"; }

protected:
  /// Wire probe/vehicle body pointers and the bridge resolver, then
  /// register every component so the framework runs loadTprm + init.
  [[nodiscard]] bool registerComponents() noexcept override;

  /// Post-init smoke check: both bodies queryable (gravity, atmosphere,
  /// terrain), logged once before the scheduler takes over.
  void configureComponents() noexcept override;

private:
  /* ----------------------------- Components ----------------------------- */

  sim::environment::celestial_body::CelestialBody earth_;
  sim::environment::celestial_body::CelestialBody moon_;
  appsim::world_query_probe::WorldQueryProbe earthProbe_;
  appsim::world_query_probe::WorldQueryProbe moonProbe_;
  appsim::ground_vehicle::GroundVehicle earthRover_;

  /// Publishes the rover's OUTPUT block to /horizon_rover. Idles when
  /// no consumer is attached.
  system_core::support::ShmRingBridge bridge_;
};

} // namespace rover_terrain
} // namespace appsim

#endif // APEX_HORIZON_DEMO_ROVER_TERRAIN_EXECUTIVE_HPP
