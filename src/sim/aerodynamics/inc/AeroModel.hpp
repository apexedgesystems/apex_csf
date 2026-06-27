#ifndef APEX_SIM_AERODYNAMICS_AERO_MODEL_HPP
#define APEX_SIM_AERODYNAMICS_AERO_MODEL_HPP
/**
 * @file AeroModel.hpp
 * @brief Aerodynamic fidelity ladder + wrench contributor.
 *
 * A common `AeroModel` interface turns the flight state into the body-frame
 * aerodynamic wrench (force + moment); concrete rungs trade fidelity for cost:
 *
 *   ConstantAero            -- fixed/zero wrench (sentinel)
 *   PolarAeroModel          -- lift + drag only (parabolic polar)
 *   StabilityDerivativeModel-- full body-frame force + moment (~30 derivatives)
 *
 * A user can add a rung by implementing `AeroModel`; nothing in the ladder is
 * closed. `AeroWrenchSource` then adapts any model into a
 * `wrench::WrenchSource` so aerodynamics stacks into the dynamics
 * `WrenchAccumulator`, tagged `WrenchKind::Aerodynamic`.
 *
 * Moment reference: a model reports its force at the body origin and its
 * moment as the couple about that origin; the `WrenchAccumulator` transfers
 * the force to the vehicle CG (parallel-axis), so the model stays CG-agnostic.
 */

#include "src/sim/aerodynamics/inc/PolarAero.hpp"
#include "src/sim/aerodynamics/inc/StabilityDerivativeAero.hpp"
#include "src/sim/dynamics/rigid_body/inc/PointMass3D.hpp" // Vec3
#include "src/sim/dynamics/wrench/inc/Wrench.hpp"

#include <cmath>

namespace sim::aerodynamics {

namespace wrench = sim::dynamics::wrench;
using sim::dynamics::rigid_body::Vec3;

/* ------------------------------ AeroModel ------------------------------ */

/**
 * Fidelity-ladder base: produce the body-frame aerodynamic wrench for the
 * current flight state. Implementations own the wind->body rotation, so the
 * returned `AppliedWrench` is always in body axes.
 */
struct AeroModel {
  virtual ~AeroModel() = default;
  [[nodiscard]] virtual wrench::AppliedWrench aeroWrench(const Vec3& v_body, const Vec3& w_body,
                                                         const ControlInputs& delta,
                                                         double rho_kg_m3) const noexcept = 0;
};

/* ------------------------------ ConstantAero ------------------------------ */

/** Simplest rung: a fixed (default zero) body-frame wrench, regardless of state. */
struct ConstantAero : AeroModel {
  wrench::AppliedWrench wrench_body{};
  [[nodiscard]] wrench::AppliedWrench aeroWrench(const Vec3&, const Vec3&, const ControlInputs&,
                                                 double) const noexcept override {
    return wrench_body;
  }
};

/* ------------------------------ PolarAeroModel ------------------------------ */

/**
 * Lift + drag only (no side force, no moments, no rate/control dependence).
 * Derives alpha from the body velocity, evaluates the parabolic polar, and
 * rotates lift/drag into body axes.
 */
struct PolarAeroModel : AeroModel {
  PolarAeroParams params;
  [[nodiscard]] wrench::AppliedWrench aeroWrench(const Vec3& v_body, const Vec3&,
                                                 const ControlInputs&,
                                                 double rho_kg_m3) const noexcept override {
    wrench::AppliedWrench out;
    const double u = v_body.x;
    const double v = v_body.y;
    const double w = v_body.z;
    const double V = std::sqrt(u * u + v * v + w * w);
    if (V < 1.0) {
      return out; // below the linearization floor: no aero load
    }
    const double alpha = std::atan2(w, u);
    const auto r = evaluatePolar(params, alpha, rho_kg_m3, V);
    out.force = windToBodyForces(r.L_N, r.D_N, /*Y*/ 0.0, alpha, /*beta*/ 0.0);
    return out;
  }
};

/* ------------------------------ StabilityDerivativeModel ------------------------------ */

/** Full linearized model: body-frame force + moment from rates and controls. */
struct StabilityDerivativeModel : AeroModel {
  StabilityDerivativeAeroParams params;
  [[nodiscard]] wrench::AppliedWrench aeroWrench(const Vec3& v_body, const Vec3& w_body,
                                                 const ControlInputs& delta,
                                                 double rho_kg_m3) const noexcept override {
    const auto r = evaluateStabilityDerivative(params, v_body, w_body, delta, rho_kg_m3);
    wrench::AppliedWrench out;
    out.force = r.force_body;
    out.moment = r.moment_body;
    return out;
  }
};

/* ------------------------------ AeroWrenchSource ------------------------------ */

/**
 * Adapts an `AeroModel` into a `wrench::WrenchSource` so aerodynamics stacks
 * into the dynamics `WrenchAccumulator`. Holds non-owning pointers to the live
 * flight inputs (body velocity, body rates, controls, density); `current()`
 * samples the model with them each tick. Tagged `WrenchKind::Aerodynamic`.
 * If any input is unwired it contributes nothing (a partially-wired vehicle
 * still produces a well-defined result).
 */
struct AeroWrenchSource : wrench::WrenchSource {
  const AeroModel* model = nullptr;
  const Vec3* v_body = nullptr;
  const Vec3* w_body = nullptr;
  const ControlInputs* controls = nullptr;
  const double* rho_kg_m3 = nullptr;

  AeroWrenchSource() {
    kind = wrench::WrenchKind::Aerodynamic;
    name = "aero";
  }

  [[nodiscard]] wrench::AppliedWrench current() const noexcept override {
    if (model == nullptr || v_body == nullptr || w_body == nullptr || controls == nullptr ||
        rho_kg_m3 == nullptr) {
      return {};
    }
    return model->aeroWrench(*v_body, *w_body, *controls, *rho_kg_m3);
  }
};

} // namespace sim::aerodynamics

#endif // APEX_SIM_AERODYNAMICS_AERO_MODEL_HPP
