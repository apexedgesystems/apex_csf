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
 * Single-pass accumulator for whole-body mass / CG / inertia-about-net-CG.
 *
 * Each contributor is folded in exactly once, referenced to the body-frame
 * origin: its mass, its first moment (m * cg), and its inertia about the
 * origin (own-CG inertia plus the parallel-axis term from its CG to the
 * origin) are summed. `finalize()` then divides out the CG and shifts the
 * summed inertia from the origin to the net CG with the reverse
 * parallel-axis term for the whole body.
 *
 * Referencing every part to a fixed point (the origin) -- rather than to
 * the net CG, which is not known until all parts are seen -- is what lets
 * the whole body combine in a single visit with no intermediate storage,
 * so the hot path never allocates. The result is algebraically identical
 * to shifting each part individually to the net CG.
 *
 *   I_about_cg = sum_i [ J_i + m_i*PA(cg_i - 0) ] - M*PA(cg - 0)
 *              = sum_i [ J_i + m_i*PA(cg_i - cg) ]
 *
 * where PA(d) is the parallel-axis tensor for offset d and the equality
 * follows from cg = sum(m_i cg_i) / M.
 *
 * If total mass is non-positive, `finalize()` returns the zero/identity
 * default (mass 0, CG at origin, identity inertia) so callers never divide
 * by zero.
 */
struct MassAggregator {
  double total_mass = 0.0;
  rigid_body::Vec3 first_moment{}; // sum(m_i * cg_i)
  rigid_body::InertiaTensor about_origin{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

  /** Fold one contributor in, referenced to the origin. */
  void add(const MassContributor& part) noexcept {
    const double m = part.mass_kg;
    const rigid_body::Vec3 r = part.cg_m;
    total_mass += m;
    first_moment = first_moment + r * m;
    about_origin.Ixx += part.inertia_about_own_cg.Ixx + m * (r.y * r.y + r.z * r.z);
    about_origin.Iyy += part.inertia_about_own_cg.Iyy + m * (r.x * r.x + r.z * r.z);
    about_origin.Izz += part.inertia_about_own_cg.Izz + m * (r.x * r.x + r.y * r.y);
    about_origin.Ixz += part.inertia_about_own_cg.Ixz + m * r.x * r.z;
    about_origin.Ixy += part.inertia_about_own_cg.Ixy + m * r.x * r.y;
    about_origin.Iyz += part.inertia_about_own_cg.Iyz + m * r.y * r.z;
  }

  /** Resolve the net mass / CG / inertia-about-CG from the running sums. */
  [[nodiscard]] AggregateMassProperties finalize() const noexcept {
    AggregateMassProperties out;
    if (total_mass <= 0.0) {
      return out; // zero mass, origin CG, identity inertia
    }
    out.mass_kg = total_mass;
    out.cg_m = first_moment * (1.0 / total_mass);
    const rigid_body::Vec3 c = out.cg_m;
    // Shift the summed inertia from the origin to the net CG.
    out.inertia_about_cg =
        rigid_body::InertiaTensor{about_origin.Ixx - total_mass * (c.y * c.y + c.z * c.z),
                                  about_origin.Iyy - total_mass * (c.x * c.x + c.z * c.z),
                                  about_origin.Izz - total_mass * (c.x * c.x + c.y * c.y),
                                  about_origin.Ixz - total_mass * c.x * c.z,
                                  about_origin.Ixy - total_mass * c.x * c.y,
                                  about_origin.Iyz - total_mass * c.y * c.z};
    return out;
  }
};

/**
 * Combine a vector of contributors. Convenience wrapper over
 * `MassAggregator` for value-list callers; the hot path uses the
 * aggregator directly to stay allocation-free.
 *
 * @param  parts  contributors (own-CG inertia each)
 * @return aggregate mass / CG / inertia about the net CG
 */
[[nodiscard]] inline AggregateMassProperties
aggregate(const std::vector<MassContributor>& parts) noexcept {
  MassAggregator agg;
  for (const auto& part : parts) {
    agg.add(part);
  }
  return agg.finalize();
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
    detail::MassAggregator agg;
    for (const auto* src : sources_) {
      if (src != nullptr) {
        agg.add(src->current());
      }
    }
    for (const auto& c : fixed_) {
      agg.add(c);
    }
    return agg.finalize();
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
