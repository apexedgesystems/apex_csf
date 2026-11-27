#ifndef APEX_BJTCOMMONEMITTER_HPP
#define APEX_BJTCOMMONEMITTER_HPP
/**
 * @file BjtCommonEmitter.hpp
 * @brief BJT common-emitter amplifier circuit model.
 *
 * Models a fixed-bias NPN common-emitter amplifier:
 *
 *     VCC ---[RC]--- Collector
 *                      |
 *                 [BJT Q1]
 *                      |
 *     VIN ---[RB]--- Base     Emitter --- GND
 *
 * DC operating point analysis (approximate):
 *   Ib = (VCC - Vbe) / RB  (assuming Vbe ~ 0.7V)
 *   Ic = beta * Ib
 *   Vce = VCC - Ic * RC
 *
 * Regions:
 *   Active:     Vce > Vce_sat (~0.2V), Ic = beta * Ib
 *   Saturation: Vce ~ Vce_sat, Ic limited by RC
 *   Cutoff:     Ib ~ 0, Ic ~ 0, Vc ~ VCC
 *
 * Uses the BjtEbersMoll model for nonlinear stamp with Newton-Raphson
 * iteration via the Circuit API.
 *
 * @note RT-safe after computeDC() (accessors only read cached state).
 */

#include "src/sim/electronics/circuit/inc/Circuit.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/BjtEbersMoll.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/Junction.hpp"

#include <cmath>

namespace sim::electronics::topologies::amplifiers {

using algorithms::mna::MnaSystem;
using algorithms::mna::NetID;
using algorithms::transient::TransientState;
using algorithms::transient::TransientStatus;
using devices::nonlinear::BjtEbersMoll;
using devices::nonlinear::BjtEbersMollParams;
using devices::nonlinear::Junction;

/* ----------------------------- BjtCommonEmitter ----------------------------- */

/**
 * @brief BJT common-emitter amplifier with fixed bias.
 *
 * Usage:
 * @code
 * BjtCommonEmitter amp(12.0, 1e3, 1e6);  // VCC=12V, RC=1k, RB=1M
 * if (amp.computeDC()) {
 *   double vc = amp.collectorVoltage();   // 10.87 V, forward active
 *   double ic = amp.collectorCurrent();   // 1.13 mA = 100 * (12 - 0.66) / 1M
 * }
 * @endcode
 */
struct BjtCommonEmitter {

  /* ----------------------------- Construction ----------------------------- */

  /**
   * @brief Construct a common-emitter amplifier.
   * @param vcc Supply voltage in volts.
   * @param rcOhms Collector resistor in ohms.
   * @param rbOhms Base resistor in ohms.
   * @param params BJT device parameters (defaults to typical 2N2222/2N3904).
   */
  BjtCommonEmitter(double vcc, double rcOhms, double rbOhms,
                   BjtEbersMollParams params = BjtEbersMollParams{}) noexcept
      : vcc_(vcc), rc_(rcOhms), rb_(rbOhms), bjtParams_(params) {
    vccNet_ = circuit_.addNet("VCC").id;
    collectorNet_ = circuit_.addNet("COLLECTOR").id;
    baseNet_ = circuit_.addNet("BASE").id;
    // Emitter connects to ground (net 0)

    circuit_.addStamp([this](MnaSystem& mna, double, const std::vector<double>& prevVoltages) {
      // VCC voltage source
      mna.addVoltageSource(vccNet_, circuit::Circuit::ground(), vcc_);

      // RC: conductance between VCC and collector
      mna.addConductance(vccNet_, collectorNet_, 1.0 / rc_);

      // RB: conductance between VCC and base
      mna.addConductance(vccNet_, baseNet_, 1.0 / rb_);

      // BJT: linearize around previous operating point
      double vb = 0.0;
      double vc = 0.0;
      if (prevVoltages.size() > baseNet_) {
        vb = prevVoltages[baseNet_];
      }
      if (prevVoltages.size() > collectorNet_) {
        vc = prevVoltages[collectorNet_];
      }

      double vbe = vb; // Emitter is ground (0V)
      double vbc = vb - vc;

      BjtEbersMoll::stamp(mna, collectorNet_, baseNet_, circuit::Circuit::ground(), vbe, vbc,
                          bjtParams_);
    });
  }

  /* ----------------------------- API ----------------------------- */

  /// Supply voltage in volts.
  /// @note RT-safe.
  [[nodiscard]] double vcc() const noexcept { return vcc_; }

  /// Collector resistance in ohms.
  /// @note RT-safe.
  [[nodiscard]] double rc() const noexcept { return rc_; }

  /// Base resistance in ohms.
  /// @note RT-safe.
  [[nodiscard]] double rb() const noexcept { return rb_; }

  /// Collector voltage after DC solve.
  /// @note RT-safe.
  [[nodiscard]] double collectorVoltage() const noexcept { return vc_; }

  /// Base voltage after DC solve.
  /// @note RT-safe.
  [[nodiscard]] double baseVoltage() const noexcept { return vb_; }

  /// Collector current after DC solve (positive = into collector).
  /// @note RT-safe.
  [[nodiscard]] double collectorCurrent() const noexcept { return ic_; }

  /**
   * @brief Compute DC operating point via Newton-Raphson iteration.
   *
   * Each iteration solves the circuit with the BJT linearized at the previous
   * junction voltages, then limits the step of both junction voltages
   * (Junction::limitVoltage) before linearizing again. The solve has
   * converged when a linear solve returns the junction voltages it was
   * linearized at.
   *
   * On success the cached collector voltage, base voltage and collector
   * current are updated. On failure they keep their previous values.
   *
   * @return True if converged, false if a linear solve failed or the
   *         iteration limit was reached.
   * @note NOT RT-safe (builds and solves circuit).
   */
  bool computeDC() {
    static constexpr int MAX_ITER = 100;
    static constexpr double TOL = 1e-9;

    const double V_CRIT = Junction::criticalVoltage(bjtParams_.Is, bjtParams_.Vt);

    circuit_.resetSolver();
    circuit_.build();

    TransientState state;
    double vbe = 0.0;
    double vbc = 0.0;

    for (int iter = 0; iter < MAX_ITER; ++iter) {
      if (circuit_.computeDC(state) != TransientStatus::SUCCESS) {
        return false;
      }

      // Emitter is ground, so the base voltage is Vbe.
      const double VBE_SOLVED = state.nodeVoltages[baseNet_];
      const double VBC_SOLVED = VBE_SOLVED - state.nodeVoltages[collectorNet_];
      if (std::abs(VBE_SOLVED - vbe) < TOL && std::abs(VBC_SOLVED - vbc) < TOL) {
        vb_ = state.nodeVoltages[baseNet_];
        vc_ = state.nodeVoltages[collectorNet_];
        ic_ = (state.nodeVoltages[vccNet_] - vc_) / rc_;
        return true;
      }

      vbe = Junction::limitVoltage(VBE_SOLVED, vbe, bjtParams_.Vt, V_CRIT);
      vbc = Junction::limitVoltage(VBC_SOLVED, vbc, bjtParams_.Vt, V_CRIT);

      // The stamp linearizes at the solver's previous voltages.
      auto& solverPrev = circuit_.solver().prevVoltages();
      solverPrev[vccNet_] = state.nodeVoltages[vccNet_];
      solverPrev[baseNet_] = vbe;
      solverPrev[collectorNet_] = vbe - vbc;
    }

    return false;
  }

  /// Underlying Circuit (for advanced configuration).
  /// @note RT-safe.
  [[nodiscard]] circuit::Circuit& circuit() noexcept { return circuit_; }

private:
  double vcc_;
  double rc_;
  double rb_;
  BjtEbersMollParams bjtParams_;
  circuit::Circuit circuit_;
  NetID vccNet_ = 0;
  NetID collectorNet_ = 0;
  NetID baseNet_ = 0;

  // Cached DC results
  double vc_ = 0.0;
  double vb_ = 0.0;
  double ic_ = 0.0;
};

} // namespace sim::electronics::topologies::amplifiers

#endif // APEX_BJTCOMMONEMITTER_HPP
