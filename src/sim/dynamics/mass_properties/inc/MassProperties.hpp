#ifndef APEX_SIM_DYNAMICS_MASS_PROPERTIES_MASS_PROPERTIES_HPP
#define APEX_SIM_DYNAMICS_MASS_PROPERTIES_MASS_PROPERTIES_HPP
/**
 * @file MassProperties.hpp
 * @brief Compositional mass-properties: aggregate per-part mass / CG /
 *        inertia into whole-body mass / CG / inertia.
 *
 * A vehicle is modeled as a collection of rigid contributors (dry
 * structure, fuel in a tank, payload, ...). Each contributor knows its
 * own mass, its CG location in a common body reference frame, and its
 * inertia tensor about its *own* CG. `MassAccumulator` combines them into
 * the whole-body mass, the mass-weighted CG, and the inertia tensor about
 * that net CG (via the parallel-axis theorem).
 *
 * This composes cleanly with `rigid_body::InertiaTensor` (full symmetric
 * form), which carries the Ixy / Iyz cross terms a parallel-axis shift
 * introduces. The output feeds straight into the 6-DOF derivative.
 *
 * Conventions match the rest of dynamics: body axes +x forward, +y
 * right, +z down; products of inertia stored positive (negated in the
 * matrix). See `rigid_body::InertiaTensor`.
 */

#include "src/sim/dynamics/rigid_body/inc/PointMass3D.hpp"   // Vec3
#include "src/sim/dynamics/rigid_body/inc/RigidBody6DOF.hpp" // InertiaTensor

#include <vector>

namespace sim::dynamics::mass_properties {

/* ------------------------------ Contributor ------------------------------ */

/**
 * One rigid mass contributor.
 *
 * @var mass_kg               mass of this part (kg)
 * @var cg_m                  CG location in the common body reference frame (m)
 * @var inertia_about_own_cg  inertia tensor about this part's own CG, body axes
 */
struct MassContributor {
  double mass_kg = 0.0;
  rigid_body::Vec3 cg_m{};
  rigid_body::InertiaTensor inertia_about_own_cg{};
};

/* ------------------------------ Aggregate ------------------------------ */

/** Whole-body mass properties produced by `MassAccumulator::result`. */
struct AggregateMassProperties {
  double mass_kg = 0.0;
  rigid_body::Vec3 cg_m{};
  rigid_body::InertiaTensor inertia_about_cg{};
};

namespace detail {

/**
 * Combine contributors into whole-body mass / CG / inertia-about-net-CG.
 *
 * Algorithm:
 *   M  = sum(m_i)
 *   cg = sum(m_i * cg_i) / M
 *   For each part, with displacement d = cg_i - cg, the net inertia is
 *   the sum of each part's own-CG inertia plus the parallel-axis term
 *   for a body of mass m_i at offset d.
 *
 * If total mass is non-positive the result is the zero/identity default
 * (mass 0, CG at origin, identity inertia) so callers never divide by
 * zero.
 *
 * Internal helper behind `MassAccumulator::result`.
 *
 * @param  parts  contributors (own-CG inertia each)
 * @return aggregate mass / CG / inertia about the net CG
 */
[[nodiscard]] inline AggregateMassProperties
aggregate(const std::vector<MassContributor>& parts) noexcept {
  AggregateMassProperties out;

  // ---- Total mass ----
  double total_mass = 0.0;
  for (const auto& part : parts) {
    total_mass += part.mass_kg;
  }
  if (total_mass <= 0.0) {
    return out; // zero mass, origin CG, identity inertia
  }
  out.mass_kg = total_mass;

  // ---- Mass-weighted CG ----
  rigid_body::Vec3 weighted{};
  for (const auto& part : parts) {
    weighted = weighted + part.cg_m * part.mass_kg;
  }
  out.cg_m = weighted * (1.0 / total_mass);

  // ---- Inertia about the net CG (own inertia + parallel-axis) ----
  // Accumulate own-inertia components first ...
  rigid_body::InertiaTensor net{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  for (const auto& part : parts) {
    net.Ixx += part.inertia_about_own_cg.Ixx;
    net.Iyy += part.inertia_about_own_cg.Iyy;
    net.Izz += part.inertia_about_own_cg.Izz;
    net.Ixz += part.inertia_about_own_cg.Ixz;
    net.Ixy += part.inertia_about_own_cg.Ixy;
    net.Iyz += part.inertia_about_own_cg.Iyz;
  }
  // ... then add the parallel-axis deltas for each part's offset.
  for (const auto& part : parts) {
    const double m = part.mass_kg;
    const rigid_body::Vec3 d = part.cg_m - out.cg_m;
    net.Ixx += m * (d.y * d.y + d.z * d.z);
    net.Iyy += m * (d.x * d.x + d.z * d.z);
    net.Izz += m * (d.x * d.x + d.y * d.y);
    net.Ixy += m * d.x * d.y;
    net.Ixz += m * d.x * d.z;
    net.Iyz += m * d.y * d.z;
  }
  out.inertia_about_cg = net;

  return out;
}

} // namespace detail

/* ------------------------------ Source layer ------------------------------ */

/**
 * Composition layer over the value aggregator.
 *
 * A `MassPropsSource` is anything that can report its *current* mass
 * contribution. This decouples "what a part is" (a fixed bracket, a
 * draining fuel tank, a slewing payload) from "how parts combine" (the
 * proven value combiner). `current()` samples the part now;
 * `MassAccumulator` collects those samples and feeds the value path.
 *
 * Sources are referenced non-owningly (raw pointers): the caller owns the
 * lifetimes and the sampling order is the caller's.
 */
struct MassPropsSource {
  virtual ~MassPropsSource() = default;
  [[nodiscard]] virtual MassContributor current() const noexcept = 0;
};

/**
 * A fixed contributor (dry structure, an installed bracket, ballast):
 * `current()` always returns the same stored `MassContributor`. Keeps the
 * common compile-time / never-changing case DRY -- no per-part subclass.
 */
struct StaticMassSource : MassPropsSource {
  MassContributor c;
  [[nodiscard]] MassContributor current() const noexcept override { return c; }
};

/**
 * Base marker for time- or state-varying sources (e.g. a fuel tank that
 * drains, a payload that shifts). Concrete dynamic sources derive from
 * this and override `current()` to report their evolving contribution.
 */
struct DynamicMassSource : MassPropsSource {};

/* ------------------------------ Accumulator ------------------------------ */

/**
 * Public stacking API for whole-body mass-property composition.
 *
 * Stack fixed `MassContributor`s and/or `MassPropsSource`s, then call
 * `result()` to sample the sources and combine everything into the
 * whole-body mass / CG / inertia about the net CG (parallel-axis). Sources
 * are sampled at each `result()` call, so a draining fuel tank or shifting
 * payload is reflected automatically between ticks.
 *
 * Sources are referenced non-owningly (raw pointers): the caller owns the
 * lifetimes. A null source pointer is skipped (defensive: a partially
 * wired vehicle still produces a well-defined aggregate).
 */
class MassAccumulator {
public:
  /** Add a fixed contribution (dry structure, ballast, an installed part). */
  void add(const MassContributor& c) { fixed_.push_back(c); }

  /** Add a source, re-sampled each `result()`. */
  void add(const MassPropsSource& s) { sources_.push_back(&s); }

  /**
   * Sample every source, append the fixed contributors, and combine into
   * the whole-body mass / CG / inertia about the net CG.
   *
   * @return aggregate mass / CG / inertia about the net CG
   */
  [[nodiscard]] AggregateMassProperties result() const noexcept {
    std::vector<MassContributor> parts;
    parts.reserve(sources_.size() + fixed_.size());
    for (const auto* src : sources_) {
      if (src != nullptr) {
        parts.push_back(src->current());
      }
    }
    parts.insert(parts.end(), fixed_.begin(), fixed_.end());
    return detail::aggregate(parts);
  }

  /** Drop all fixed contributors and sources. */
  void clear() {
    fixed_.clear();
    sources_.clear();
  }

private:
  std::vector<MassContributor> fixed_;
  std::vector<const MassPropsSource*> sources_;
};

} // namespace sim::dynamics::mass_properties

#endif // APEX_SIM_DYNAMICS_MASS_PROPERTIES_MASS_PROPERTIES_HPP
