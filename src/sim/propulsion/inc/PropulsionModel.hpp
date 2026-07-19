#ifndef APEX_SIM_PROPULSION_PROPULSION_MODEL_HPP
#define APEX_SIM_PROPULSION_PROPULSION_MODEL_HPP
/**
 * @file PropulsionModel.hpp
 * @brief Propulsion fidelity ladder + the PropulsionSystem contributor.
 *
 * A common `PropulsionModel` interface advances the engine one tick and reports
 * per-engine thrust (and rotor angular momentum); concrete rungs trade fidelity
 * for cost:
 *
 *   ConstantThrust           -- fixed thrust (sentinel)
 *   DensityScaledThrustModel -- empirical density-scaled thrust (stateless)
 *   Turbofan2SpoolModel      -- two-spool dynamics + rotor momentum (stateful)
 *
 * `PropulsionSystem` is the construct that links propulsion's two contribution
 * types. Propulsion spans *both* dynamics stacks:
 *
 *   - the WRENCH stack -- thrust (a body-frame force) plus the gyroscopic
 *     moment of the spinning rotors;
 *   - the MASS stack   -- fuel that depletes (a dynamic mass source) and the
 *     engine/tank structure (a static mass source).
 *
 * The mass models already live in `mass_properties` (FuelTankMassSource,
 * StaticMassSource); they are not duplicated here. PropulsionSystem is the
 * link: it *is* a WrenchSource (the thrust + gyro face), and each `step()` it
 * computes thrust once and drives the fuel tank's burn from that same thrust.
 * The caller adds the system to the WrenchAccumulator and the fuel tank to the
 * MassAccumulator -- one construct, two stacks, separated by contribution.
 */

#include "src/sim/dynamics/mass_properties/inc/FuelBurnMassProperties.hpp" // FuelTankMassSource
#include "src/sim/dynamics/rigid_body/inc/PointMass3D.hpp"                 // Vec3
#include "src/sim/dynamics/rigid_body/inc/PointMass3D.hpp"                 // Vec3 + cross
#include "src/sim/dynamics/wrench/inc/Wrench.hpp"
#include "src/sim/propulsion/inc/DensityScaledThrust.hpp"
#include "src/sim/propulsion/inc/Turbofan2Spool.hpp"

namespace sim::propulsion {

namespace wrench = sim::dynamics::wrench;
namespace mass_properties = sim::dynamics::mass_properties;
using sim::dynamics::rigid_body::cross;
using sim::dynamics::rigid_body::Vec3;

/* ------------------------------ PropulsionModel ------------------------------ */

/** Per-engine propulsion output for one tick. */
struct PropulsionOutput {
  double thrust_N = 0.0;       // per-engine thrust (N)
  double H_rotor_kgm2_s = 0.0; // per-engine rotor angular momentum (0 if none)
};

/**
 * Fidelity-ladder base: advance one tick and report per-engine thrust (+ rotor
 * angular momentum). Stateful rungs (spool dynamics) update internal state;
 * stateless rungs ignore dt. `step` is non-const because stateful rungs mutate.
 * A user adds a rung by implementing `PropulsionModel`.
 */
struct PropulsionModel {
  virtual ~PropulsionModel() = default;
  [[nodiscard]] virtual PropulsionOutput step(double throttle, double rho_kg_m3,
                                              double dt_s) noexcept = 0;
};

/** Simplest rung: a fixed thrust regardless of throttle / density. */
struct ConstantThrust : PropulsionModel {
  double thrust_N = 0.0;
  [[nodiscard]] PropulsionOutput step(double, double, double) noexcept override {
    return PropulsionOutput{thrust_N, 0.0};
  }
};

/** Density-scaled empirical thrust (stateless). */
struct DensityScaledThrustModel : PropulsionModel {
  DensityScaledThrustParams params;
  [[nodiscard]] PropulsionOutput step(double throttle, double rho_kg_m3, double) noexcept override {
    return PropulsionOutput{evaluateThrust(params, throttle, rho_kg_m3), 0.0};
  }
};

/** Two-spool turbofan with spool dynamics + rotor momentum (stateful). */
struct Turbofan2SpoolModel : PropulsionModel {
  Turbofan2SpoolParams params;
  Turbofan2SpoolState state;
  [[nodiscard]] PropulsionOutput step(double throttle, double rho_kg_m3,
                                      double dt_s) noexcept override {
    const auto r = stepTurbofan2Spool(state, params, throttle, rho_kg_m3, dt_s);
    return PropulsionOutput{r.thrust_N, r.H_rotor_kgm2_s};
  }
};

/* ------------------------------ PropulsionSystem ------------------------------ */

/**
 * The construct that links propulsion's wrench and mass contributions.
 *
 * It is a `wrench::WrenchSource` (the thrust + gyroscopic face, tagged
 * `Propulsion`). `step()` advances the thrust model once per tick, scales the
 * per-engine output by `engine_count`, and drives the optional fuel tank's burn
 * from the produced thrust -- so one quantity (thrust) feeds both stacks. The
 * caller adds this system to the WrenchAccumulator and the same fuel tank to
 * the MassAccumulator; the engine/tank structure is a separate static mass
 * source the caller owns.
 *
 * Non-owning pointers: the caller owns the model, fuel tank, and the body-rate
 * input. If `model` is unwired the system contributes nothing; if `fuel` is
 * null no fuel is burned; if `omega_body` is null no gyroscopic moment is added.
 */
struct PropulsionSystem : wrench::WrenchSource {
  PropulsionModel* model = nullptr;
  mass_properties::FuelTankMassSource* fuel = nullptr; // driven from thrust each step
  const Vec3* omega_body = nullptr;                    // body rates, for the gyro moment

  Vec3 thrust_axis_body{1.0, 0.0, 0.0}; // thrust direction (default body +x forward)
  Vec3 mount_m{};                       // thrust application point (body frame)
  Vec3 spin_axis_body{1.0, 0.0, 0.0};   // rotor spin axis (default body +x)
  int engine_count = 1;                 // identical engines summed

  // Cached after step().
  double thrust_N_total = 0.0;
  double H_rotor_total = 0.0;

  PropulsionSystem() {
    kind = wrench::WrenchKind::Propulsion;
    name = "propulsion";
  }

  /**
   * Advance the engines one tick and drive fuel burn from the produced thrust.
   *
   * @param throttle   command in [0, 1]
   * @param rho_kg_m3  ambient density
   * @param dt_s       step (s)
   */
  void step(double throttle, double rho_kg_m3, double dt_s) noexcept {
    if (model == nullptr) {
      thrust_N_total = 0.0;
      H_rotor_total = 0.0;
      return;
    }
    const PropulsionOutput out = model->step(throttle, rho_kg_m3, dt_s);
    const double n = static_cast<double>(engine_count);
    thrust_N_total = out.thrust_N * n;
    H_rotor_total = out.H_rotor_kgm2_s * n;
    if (fuel != nullptr) {
      fuel->step(thrust_N_total, dt_s); // the link: thrust drives the mass side
    }
  }

  /** Thrust force at the mount + the gyroscopic moment from the last step. */
  [[nodiscard]] wrench::AppliedWrench current() const noexcept override {
    wrench::AppliedWrench w;
    w.force = thrust_axis_body * thrust_N_total;
    w.point_m = mount_m;
    if (omega_body != nullptr) {
      // Gyroscopic moment of the spinning rotors: H x omega_body.
      const Vec3 H = spin_axis_body * H_rotor_total;
      w.moment = cross(H, *omega_body);
    }
    return w;
  }
};

} // namespace sim::propulsion

#endif // APEX_SIM_PROPULSION_PROPULSION_MODEL_HPP
