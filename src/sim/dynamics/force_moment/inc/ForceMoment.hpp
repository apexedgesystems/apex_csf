#ifndef APEX_SIM_DYNAMICS_FORCE_MOMENT_FORCE_MOMENT_HPP
#define APEX_SIM_DYNAMICS_FORCE_MOMENT_FORCE_MOMENT_HPP
/**
 * @file ForceMoment.hpp
 * @brief Compositional force/moment aggregation -- the symmetric pattern
 *        to mass_properties.
 *
 * Where `mass_properties::MassAccumulator` stacks per-part mass / CG /
 * inertia into whole-body mass properties, `ForceMomentAccumulator` stacks
 * per-source applied forces (each at a point, plus an optional pure couple)
 * into the net force and net moment about a chosen reference point.
 *
 * A force applied off the reference point induces a moment r x F where
 * r = point - about. Pure couples add directly. The result feeds straight
 * into the 6-DOF derivative as (force_body, moment_body about CG) when the
 * reference point is the vehicle CG.
 *
 * Conventions match the rest of dynamics: body axes +x forward, +y right,
 * +z down. All quantities are body-frame here. See `rigid_body::Vec3` and
 * `rigid_body::cross`.
 */

#include "src/sim/dynamics/rigid_body/inc/PointMass3D.hpp"   // Vec3
#include "src/sim/dynamics/rigid_body/inc/RigidBody6DOF.hpp" // cross

#include <vector>

namespace sim::dynamics::force_moment {

/* ------------------------------ AppliedForce ------------------------------ */

/**
 * A force applied at a point, plus an optional pure couple.
 *
 * @var force    force vector (N), body frame
 * @var point_m  application point (m), body frame
 * @var moment   pure couple applied by this part (N*m), body frame --
 *               independent of the reference point (a couple is the same
 *               about any point)
 */
struct AppliedForce {
  rigid_body::Vec3 force{};
  rigid_body::Vec3 point_m{};
  rigid_body::Vec3 moment{};
};

/* ------------------------------ ForceMoment ------------------------------ */

/**
 * Net force + net moment about a reference point.
 *
 * @var force   sum of applied forces (N), body frame
 * @var moment  net moment about the reference point (N*m), body frame
 */
struct ForceMoment {
  rigid_body::Vec3 force{};
  rigid_body::Vec3 moment{};
};

namespace detail {

/**
 * Combine applied forces into a net force + net moment about `about`.
 *
 *   force  = sum( part.force )
 *   moment = sum( part.moment + (part.point_m - about) x part.force )
 *
 * Internal helper behind `ForceMomentAccumulator::resultAbout`.
 *
 * @param parts  applied forces (force-at-point + optional couple)
 * @param about  reference point the net moment is taken about (m)
 * @return net force + net moment about `about`
 */
[[nodiscard]] inline ForceMoment combine(const std::vector<AppliedForce>& parts,
                                         const rigid_body::Vec3& about) noexcept {
  ForceMoment out;
  for (const auto& part : parts) {
    out.force = out.force + part.force;
    const rigid_body::Vec3 r = part.point_m - about;
    const rigid_body::Vec3 induced = rigid_body::cross(r, part.force);
    out.moment = out.moment + part.moment + induced;
  }
  return out;
}

} // namespace detail

/* ------------------------------ Source layer ------------------------------ */

/**
 * Composition layer over the value combiner, mirroring `MassPropsSource`.
 *
 * A `ForceMomentSource` reports the force/couple it applies *now*. This
 * decouples "what applies a load" (a fixed strut, a throttled engine, an
 * aero surface) from "how loads combine". Sources are referenced
 * non-owningly; the caller owns the lifetimes and the sampling order.
 */
struct ForceMomentSource {
  virtual ~ForceMomentSource() = default;
  [[nodiscard]] virtual AppliedForce current() const noexcept = 0;
};

/**
 * A fixed applied force (a static strut load, ballast pull, a calibrated
 * test force): `current()` always returns the stored `AppliedForce`.
 */
struct StaticForceMomentSource : ForceMomentSource {
  AppliedForce f;
  [[nodiscard]] AppliedForce current() const noexcept override { return f; }
};

/**
 * Base marker for time- or state-varying force/moment sources (a throttled
 * engine, an aero force that tracks angle of attack). Concrete dynamic
 * sources derive from this and override `current()`.
 */
struct DynamicForceMomentSource : ForceMomentSource {};

/* ------------------------------ Accumulator ------------------------------ */

/**
 * Public stacking API for force/moment composition.
 *
 * Stack fixed `AppliedForce`s and/or `ForceMomentSource`s, then call
 * `resultAbout()` to sample the sources and combine everything into the
 * net force + net moment about a reference point. Sources are sampled at
 * each `resultAbout()` call, so a load that varies between ticks is
 * reflected automatically.
 *
 * Sources are referenced non-owningly (raw pointers): the caller owns the
 * lifetimes. A null source pointer is skipped (defensive: a partially
 * wired vehicle still produces a well-defined result).
 */
class ForceMomentAccumulator {
public:
  /** Add a fixed applied force (force-at-point + optional couple). */
  void add(const AppliedForce& f) { fixed_.push_back(f); }

  /** Add a source, re-sampled each `resultAbout()`. */
  void add(const ForceMomentSource& s) { sources_.push_back(&s); }

  /**
   * Sample every source, append the fixed loads, and combine into the net
   * force + net moment about `about`.
   *
   * @param about  reference point the net moment is taken about (m)
   * @return net force + net moment about `about`
   */
  [[nodiscard]] ForceMoment resultAbout(const rigid_body::Vec3& about) const noexcept {
    std::vector<AppliedForce> parts;
    parts.reserve(sources_.size() + fixed_.size());
    for (const auto* src : sources_) {
      if (src != nullptr) {
        parts.push_back(src->current());
      }
    }
    parts.insert(parts.end(), fixed_.begin(), fixed_.end());
    return detail::combine(parts, about);
  }

  /** Drop all fixed loads and sources. */
  void clear() {
    fixed_.clear();
    sources_.clear();
  }

private:
  std::vector<AppliedForce> fixed_;
  std::vector<const ForceMomentSource*> sources_;
};

} // namespace sim::dynamics::force_moment

#endif // APEX_SIM_DYNAMICS_FORCE_MOMENT_FORCE_MOMENT_HPP
