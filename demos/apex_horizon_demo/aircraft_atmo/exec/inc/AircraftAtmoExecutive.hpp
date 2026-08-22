#ifndef APEX_HORIZON_DEMO_AIRCRAFT_ATMO_EXECUTIVE_HPP
#define APEX_HORIZON_DEMO_AIRCRAFT_ATMO_EXECUTIVE_HPP
/**
 * @file AircraftAtmoExecutive.hpp
 * @brief Executive for the aircraft_atmo demo.
 *
 * Composes the atmosphere-only world and the closed-loop aircraft:
 *   - One passive CelestialBody (Earth: J2 gravity, ellipsoid terrain,
 *     layered USSA76 atmosphere), configured via TPRM tunables.
 *   - The Aircraft flying full 6DOF at 50 Hz.
 *   - The AircraftController's six-loop autopilot at 25 Hz.
 *   - One ShmRingBridge carrying the ACFT/1 link on /horizon_aircraft:
 *     256-byte frames out at 50 Hz, APROTO commands drained from the
 *     reverse ring. The bridge idles if no consumer attaches; the sim
 *     runs unchanged.
 *
 * The executive's C++ specifies cross-component wiring only (the
 * aircraft's body + controller pointers, the controller's aircraft
 * pointer, the bridge's registry resolver); all configuration values
 * come from the TPRM set.
 */

#include "demos/apex_horizon_demo/aircraft/inc/Aircraft.hpp"
#include "demos/apex_horizon_demo/aircraft_controller/inc/AircraftController.hpp"
#include "src/sim/environment/celestial_body/inc/CelestialBody.hpp"
#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/support/shm_ring_bridge/inc/ShmRingBridge.hpp"

namespace appsim {
namespace aircraft_atmo {

/* ----------------------------- AircraftAtmoExecutive ----------------------------- */

class AircraftAtmoExecutive : public executive::ApexExecutive {
public:
  using ApexExecutive::ApexExecutive;

  ~AircraftAtmoExecutive() override = default;

  [[nodiscard]] const char* label() const noexcept override { return "AIRCRAFT_ATMO_EXEC"; }

protected:
  /// Wire the aircraft/controller/body pointers and the bridge
  /// resolver, then register every component so the framework runs
  /// loadTprm + init.
  [[nodiscard]] bool registerComponents() noexcept override;

  /// Post-init smoke check: the body queryable (gravity, atmosphere at
  /// the cruise altitude), logged once before the scheduler takes over.
  void configureComponents() noexcept override;

private:
  /* ----------------------------- Components ----------------------------- */

  sim::environment::celestial_body::CelestialBody earth_;
  appsim::aircraft::Aircraft aircraft_;
  appsim::aircraft_controller::AircraftController controller_;

  /// Publishes the aircraft's OUTPUT block to /horizon_aircraft and
  /// drains inbound commands. Idles when no consumer is attached.
  system_core::support::ShmRingBridge bridge_;
};

} // namespace aircraft_atmo
} // namespace appsim

#endif // APEX_HORIZON_DEMO_AIRCRAFT_ATMO_EXECUTIVE_HPP
