#ifndef APEX_MOSFETLEVEL1_HPP
#define APEX_MOSFETLEVEL1_HPP
/**
 * @file MosfetLevel1.hpp
 * @brief MOSFET Level 1 model (Shichman-Hodges 3-region analog model).
 *
 * Implements the classic Shichman-Hodges MOSFET equations with three regions:
 *
 * 1. Cutoff (Vgs < Vth): Id = 0
 * 2. Linear (Vgs > Vth, Vds < Vgs - Vth): Id = Kp * ((Vgs - Vth) * Vds - 0.5 * Vds^2)
 * 3. Saturation (Vgs > Vth, Vds >= Vgs - Vth): Id = 0.5 * Kp * (Vgs - Vth)^2
 *
 * Where:
 *   Kp = transconductance parameter (A/V^2), typically 20u-200u for NMOS
 *   Vth = threshold voltage (V), typically 0.5-1.5V for NMOS
 *
 * This is a PHYSICS MODEL (Layer 2) for analog circuit simulation with
 * smooth I-V curves and derivatives for Newton-Raphson convergence.
 *
 * RT-safety: RT-safe (static functions, no allocations).
 * Thread-safety: Safe (stateless, pure functions).
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_attrs.hpp"

#include <cmath>

namespace sim::electronics::devices::nonlinear {

using algorithms::mna::MnaSystem;
using algorithms::mna::NetID;

/**
 * @brief MOSFET Level 1 model parameters.
 *
 * Physical parameters defining N-channel MOSFET behavior. Default values
 * represent a typical NMOS transistor.
 */
struct MosfetLevel1Params {
  double Kp = 100e-6;  ///< Transconductance parameter (A/V^2), typical 20u-200u
  double Vth = 0.7;    ///< Threshold voltage (V), typical 0.5-1.5V
  double lambda = 0.0; ///< Channel-length modulation (1/V), 0 = ideal, typical 0.01-0.1

  /// Subthreshold smoothing voltage. Below Vth, current transitions exponentially
  /// to zero over this range. Prevents the Jacobian discontinuity at Vth that
  /// causes NR convergence failures. Set to 0 for hard cutoff (original behavior).
  double Vsmooth = 0.1; ///< Smoothing range below Vth (V). 0 = disabled.
};

/**
 * @brief MOSFET Level 1 physics model.
 *
 * Provides drain current and transconductances (gm, gds) for use with
 * Newton-Raphson nonlinear solver.
 *
 * Usage:
 * @code
 * MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.02};
 *
 * // Compute drain current at operating point
 * double vgs = 1.5, vds = 2.0;
 * double id = MosfetLevel1::current(vgs, vds, params);
 *
 * // Compute transconductances for Newton-Raphson
 * double gm = MosfetLevel1::transconductance(vgs, vds, params);
 * double gds = MosfetLevel1::outputConductance(vgs, vds, params);
 *
 * // Stamp into MNA system
 * MosfetLevel1::stamp(mna, drainNet, gateNet, sourceNet, vgs, vds, params);
 * @endcode
 */
struct MosfetLevel1 {
  /**
   * @brief Combined (id, gm, gds) from a single region evaluation.
   *
   * Returned by `stampValues` so that a caller who needs all three
   * Newton-Raphson stamp quantities (drain current, transconductance,
   * output conductance) pays the cost of the region-branch and
   * shared-subexpression math exactly once instead of three times.
   */
  struct StampValues {
    double id;  ///< Drain current.
    double gm;  ///< dId/dVgs.
    double gds; ///< dId/dVds.
  };

  /**
   * @brief Compute {id, gm, gds} together.
   *
   * Equivalent to calling `current`, `transconductance`, and
   * `outputConductance` successively, but evaluates the region branch
   * and shared subexpressions (VGST, LAMBDA_FACTOR, ratio, id_at_th_base)
   * exactly once. The 2242-MOSFET Intel 4004 NR stamp path invokes all
   * three per transistor per iteration; this combined call is the fast
   * path for that workload.
   *
   * @note RT-safe (pure math, no allocations).
   */
  [[nodiscard]] SIM_HD_FI static StampValues stampValues(double vgs, double vds,
                                                      const MosfetLevel1Params& params) noexcept {
    if (vgs <= params.Vth - params.Vsmooth) {
      return {0.0, 0.0, 0.0};
    }

    if (vgs < params.Vth && params.Vsmooth > 0.0) {
      const double VGST_EFF = vgs - (params.Vth - params.Vsmooth);
      const double RATIO = VGST_EFF / params.Vsmooth;
      const double ID_AT_TH_BASE = 0.5 * params.Kp * params.Vsmooth * params.Vsmooth;
      const double LAMBDA_FACTOR = 1.0 + params.lambda * vds;
      const double ID_AT_TH = ID_AT_TH_BASE * LAMBDA_FACTOR;
      return {ID_AT_TH * RATIO * RATIO, ID_AT_TH * 2.0 * RATIO / params.Vsmooth,
              ID_AT_TH_BASE * params.lambda * RATIO * RATIO};
    }

    const double VGST = vgs - params.Vth;
    const double LAMBDA_FACTOR = 1.0 + params.lambda * vds;

    if (vds < VGST) {
      // Linear region.
      const double ID_BASE = params.Kp * (VGST * vds - 0.5 * vds * vds);
      const double DID_DVDS_BASE = params.Kp * (VGST - vds);
      return {ID_BASE * LAMBDA_FACTOR, params.Kp * vds * LAMBDA_FACTOR,
              DID_DVDS_BASE * LAMBDA_FACTOR + ID_BASE * params.lambda};
    }

    // Saturation region.
    const double ID_SAT = 0.5 * params.Kp * VGST * VGST;
    return {ID_SAT * LAMBDA_FACTOR, params.Kp * VGST * LAMBDA_FACTOR, ID_SAT * params.lambda};
  }

  /**
   * @brief Compute drain current using Shichman-Hodges equations.
   *
   * Returns Id for the three operating regions:
   * - Cutoff: Id = 0
   * - Linear: Id = Kp * ((Vgs - Vth) * Vds - 0.5 * Vds^2) * (1 + lambda * Vds)
   * - Saturation: Id = 0.5 * Kp * (Vgs - Vth)^2 * (1 + lambda * Vds)
   *
   * @param vgs Gate-source voltage in volts.
   * @param vds Drain-source voltage in volts.
   * @param params MOSFET physical parameters.
   * @return Drain current in amperes.
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double current(double vgs, double vds,
                                      const MosfetLevel1Params& params) noexcept {
    // Deep cutoff (well below threshold)
    if (vgs <= params.Vth - params.Vsmooth) {
      return 0.0;
    }

    // Subthreshold smoothing region: exponential transition to zero
    if (vgs < params.Vth && params.Vsmooth > 0.0) {
      // Current at threshold (vgst=0, saturation boundary)
      // Use a small virtual overdrive to avoid zero current at exact threshold
      constexpr double VT = 0.026; // Thermal voltage
      double vgst_eff = vgs - (params.Vth - params.Vsmooth);
      double ratio = vgst_eff / params.Vsmooth; // 0 at deep cutoff, 1 at Vth
      // Quadratic smoothing: id = id_threshold * ratio^2
      double id_at_th =
          0.5 * params.Kp * params.Vsmooth * params.Vsmooth * (1.0 + params.lambda * vds);
      return id_at_th * ratio * ratio;
    }

    const double VGST = vgs - params.Vth; // Gate overdrive voltage
    const double LAMBDA_FACTOR = 1.0 + params.lambda * vds;

    // Linear region
    if (vds < VGST) {
      return params.Kp * (VGST * vds - 0.5 * vds * vds) * LAMBDA_FACTOR;
    }

    // Saturation region
    return 0.5 * params.Kp * VGST * VGST * LAMBDA_FACTOR;
  }

  /**
   * @brief Compute transconductance gm = dId/dVgs for Newton-Raphson.
   *
   * @param vgs Gate-source voltage in volts.
   * @param vds Drain-source voltage in volts.
   * @param params MOSFET physical parameters.
   * @return Transconductance in siemens.
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double transconductance(double vgs, double vds,
                                               const MosfetLevel1Params& params) noexcept {
    if (vgs <= params.Vth - params.Vsmooth) {
      return 0.0;
    }

    // Subthreshold: derivative of quadratic smoothing
    if (vgs < params.Vth && params.Vsmooth > 0.0) {
      double vgst_eff = vgs - (params.Vth - params.Vsmooth);
      double ratio = vgst_eff / params.Vsmooth;
      double id_at_th =
          0.5 * params.Kp * params.Vsmooth * params.Vsmooth * (1.0 + params.lambda * vds);
      // d(id)/d(vgs) = id_at_th * 2*ratio / Vsmooth
      return id_at_th * 2.0 * ratio / params.Vsmooth;
    }

    const double VGST = vgs - params.Vth;
    const double LAMBDA_FACTOR = 1.0 + params.lambda * vds;

    if (vds < VGST) {
      return params.Kp * vds * LAMBDA_FACTOR;
    }

    return params.Kp * VGST * LAMBDA_FACTOR;
  }

  /**
   * @brief Compute output conductance gds = dId/dVds for Newton-Raphson.
   *
   * @param vgs Gate-source voltage in volts.
   * @param vds Drain-source voltage in volts.
   * @param params MOSFET physical parameters.
   * @return Output conductance in siemens.
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double outputConductance(double vgs, double vds,
                                                const MosfetLevel1Params& params) noexcept {
    if (vgs <= params.Vth - params.Vsmooth) {
      return 0.0;
    }

    // Subthreshold: d(id)/d(vds) of quadratic smoothing
    if (vgs < params.Vth && params.Vsmooth > 0.0) {
      double vgst_eff = vgs - (params.Vth - params.Vsmooth);
      double ratio = vgst_eff / params.Vsmooth;
      double id_at_th_base = 0.5 * params.Kp * params.Vsmooth * params.Vsmooth;
      // id = id_at_th_base * (1 + lambda*vds) * ratio^2
      // d(id)/d(vds) = id_at_th_base * lambda * ratio^2
      return id_at_th_base * params.lambda * ratio * ratio;
    }

    const double VGST = vgs - params.Vth;
    const double LAMBDA_FACTOR = 1.0 + params.lambda * vds;

    if (vds < VGST) {
      const double ID_BASE = params.Kp * (VGST * vds - 0.5 * vds * vds);
      const double DID_DVDS_BASE = params.Kp * (VGST - vds);
      return DID_DVDS_BASE * LAMBDA_FACTOR + ID_BASE * params.lambda;
    }

    const double ID_SAT = 0.5 * params.Kp * VGST * VGST;
    return ID_SAT * params.lambda;
  }

  /**
   * @brief Stamp NMOS MOSFET into MNA system for Newton-Raphson iteration.
   *
   * Linearizes MOSFET around current operating point:
   *   Id = gm * Vgs + gds * Vds + Ieq
   *   where: gm = dId/dVgs, gds = dId/dVds, Ieq = Id - gm*Vgs - gds*Vds
   *
   * The gm term is a VCCS (asymmetric): gate voltage controls drain current.
   * Uses addGEntry for asymmetric stamps, addConductance for symmetric gds.
   *
   * @param mna MNA system to stamp into.
   * @param drainNet Drain net ID.
   * @param gateNet Gate net ID.
   * @param sourceNet Source net ID.
   * @param vgs Current gate-source voltage (from previous iteration).
   * @param vds Current drain-source voltage (from previous iteration).
   * @param params MOSFET physical parameters.
   *
   * @note RT-safe (stamps into pre-allocated matrix/vector).
   */
  static void stamp(MnaSystem& mna, NetID drainNet, NetID gateNet, NetID sourceNet, double vgs,
                    double vds, const MosfetLevel1Params& params) {
    const auto SV = stampValues(vgs, vds, params);
    const double ID = SV.id;
    const double GM = SV.gm;
    const double GDS = SV.gds;
    const double IEQ = ID - GM * vgs - GDS * vds;

    // Symmetric gds conductance (drain-source resistance)
    mna.addConductance(drainNet, sourceNet, GDS);

    // Asymmetric gm entries (gate-controlled current source)
    mna.addGEntry(drainNet, gateNet, GM);
    mna.addGEntry(drainNet, sourceNet, -GM);
    mna.addGEntry(sourceNet, gateNet, -GM);
    mna.addGEntry(sourceNet, sourceNet, GM);

    // Compensation current: idEq leaves drain, enters source
    mna.addCurrent(drainNet, sourceNet, -IEQ);
  }

  /**
   * @brief Stamp an NMOS transistor into MNA with full NR linearization.
   *
   * Handles reverse mode (VDS < 0) automatically by swapping drain/source
   * roles. Uses GMIN-separated compensation current: the GMIN stabilizer
   * conductance is stamped but excluded from the compensation current so
   * it doesn't cancel itself out.
   *
   * @param mna MNA system to stamp into.
   * @param drainNet NMOS drain net ID.
   * @param gateNet Gate net ID.
   * @param sourceNet NMOS source net ID (typically ground).
   * @param prevV Previous node voltages (index = net ID, 0 = ground).
   * @param params NMOS model parameters.
   * @param gmin Minimum conductance for numerical stability.
   *
   * @note RT-safe (stamps into pre-allocated matrix/vector).
   */
  static void stampNmos(MnaSystem& mna, NetID drainNet, NetID gateNet, NetID sourceNet,
                        const std::vector<double>& prevV, const MosfetLevel1Params& params,
                        double gmin = 1e-12) {
    auto netV = [&](NetID n) { return (n > 0 && n < prevV.size()) ? prevV[n] : 0.0; };
    double vS = netV(sourceNet);
    double vG = netV(gateNet);
    double vD = netV(drainNet);
    double vgs = vG - vS;
    double vds = vD - vS;

    bool reversed = (vds < 0.0);
    NetID dEff = reversed ? sourceNet : drainNet;
    NetID sEff = reversed ? drainNet : sourceNet;
    double evalVgs = reversed ? (vG - vD) : vgs;
    double evalVds = reversed ? (vS - vD) : vds;
    double vgsC = std::max(evalVgs, 0.0);
    double vdsC = std::max(evalVds, 0.0);

    const auto SV = stampValues(vgsC, vdsC, params);
    double id = SV.id;
    double gm = SV.gm;
    double gdsDevice = SV.gds;
    double gdsStamp = std::max(gdsDevice, gmin);
    double idEq = id - gm * evalVgs - gdsDevice * evalVds;

    mna.addConductance(dEff, sEff, gdsStamp);
    mna.addGEntry(dEff, gateNet, gm);
    mna.addGEntry(dEff, sEff, -gm);
    mna.addGEntry(sEff, gateNet, -gm);
    mna.addGEntry(sEff, sEff, gm);
    mna.addCurrent(dEff, sEff, -idEq);
  }

  /**
   * @brief Stamp a PMOS transistor into MNA with full NR linearization.
   *
   * Uses VSG = V(source) - V(gate) and VSD = V(source) - V(drain) convention.
   * Current flows from source (VDD) to drain (output). Handles reverse mode
   * automatically. GMIN-separated compensation current.
   *
   * @param mna MNA system to stamp into.
   * @param sourceNet PMOS source net ID (typically VDD).
   * @param gateNet Gate net ID.
   * @param drainNet PMOS drain net ID (output side).
   * @param prevV Previous node voltages (index = net ID, 0 = ground).
   * @param params PMOS model parameters (positive Vth).
   * @param gmin Minimum conductance for numerical stability.
   *
   * @note RT-safe (stamps into pre-allocated matrix/vector).
   */
  static void stampPmos(MnaSystem& mna, NetID sourceNet, NetID gateNet, NetID drainNet,
                        const std::vector<double>& prevV, const MosfetLevel1Params& params,
                        double gmin = 1e-12) {
    auto netV = [&](NetID n) { return (n > 0 && n < prevV.size()) ? prevV[n] : 0.0; };
    double vS = netV(sourceNet);
    double vG = netV(gateNet);
    double vD = netV(drainNet);
    double vsg = vS - vG;
    double vsd = vS - vD;

    bool reversed = (vsd < 0.0);
    NetID sEff = reversed ? drainNet : sourceNet;
    NetID dEff = reversed ? sourceNet : drainNet;
    double evalVsg = reversed ? (vD - vG) : vsg;
    double evalVsd = reversed ? (vD - vS) : vsd;
    double vsgC = std::max(evalVsg, 0.0);
    double vsdC = std::max(evalVsd, 0.0);

    const auto SV = stampValues(vsgC, vsdC, params);
    double id = SV.id;
    double gm = SV.gm;
    double gdsDevice = SV.gds;
    double gdsStamp = std::max(gdsDevice, gmin);
    double idEq = id - gm * evalVsg - gdsDevice * evalVsd;

    mna.addConductance(dEff, sEff, gdsStamp);
    mna.addGEntry(dEff, sEff, -gm);
    mna.addGEntry(dEff, gateNet, gm);
    mna.addGEntry(sEff, sEff, gm);
    mna.addGEntry(sEff, gateNet, -gm);
    mna.addCurrent(dEff, sEff, idEq);
  }

  /**
   * @brief Determine operating region for diagnostic purposes.
   *
   * @param vgs Gate-source voltage in volts.
   * @param vds Drain-source voltage in volts.
   * @param params MOSFET physical parameters.
   * @return 0 = cutoff, 1 = linear, 2 = saturation.
   */
  /**
   * @brief Limit Vgs change per NR iteration (from ngspice DEVfetlim).
   *
   * Prevents excessive gate voltage swings near threshold that cause
   * NR oscillation. Adapts step size based on distance from Vth.
   * Direct translation from Berkeley SPICE3 / ngspice devsup.c.
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double fetlim(double vnew, double vold, double vto) noexcept {
    double vtsthi = std::fabs(2.0 * (vold - vto)) + 2.0;
    double vtstlo = std::fabs(vold - vto) + 1.0;
    double vtox = vto + 3.5;
    double delv = vnew - vold;

    if (vold >= vto) {
      if (vold >= vtox) {
        if (delv <= 0.0) {
          if (vnew >= vtox) {
            if (-delv > vtstlo)
              vnew = vold - vtstlo;
          } else {
            vnew = std::max(vnew, vto + 2.0);
          }
        } else {
          if (delv >= vtsthi)
            vnew = vold + vtsthi;
        }
      } else {
        if (delv <= 0.0) {
          vnew = std::max(vnew, vto - 0.5);
        } else {
          vnew = std::min(vnew, vto + 4.0);
        }
      }
    } else {
      if (delv <= 0.0) {
        if (-delv > vtsthi)
          vnew = vold - vtsthi;
      } else {
        double vtemp = vto + 0.5;
        if (vnew <= vtemp) {
          if (delv > vtstlo)
            vnew = vold + vtstlo;
        } else {
          vnew = vtemp;
        }
      }
    }
    return vnew;
  }

  /**
   * @brief Limit Vds change per NR iteration (from ngspice DEVlimvds).
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double limvds(double vnew, double vold) noexcept {
    if (vold >= 3.5) {
      if (vnew > vold) {
        vnew = std::min(vnew, 3.0 * vold + 2.0);
      } else if (vnew < 3.5) {
        vnew = std::max(vnew, 2.0);
      }
    } else {
      if (vnew > vold) {
        vnew = std::min(vnew, 4.0);
      } else {
        vnew = std::max(vnew, -0.5);
      }
    }
    return vnew;
  }

  /**
   * @brief Classify MOSFET operating region.
   * @return -1 = deep cutoff, 0 = subthreshold, 1 = linear, 2 = saturation.
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static int region(double vgs, double vds,
                                  const MosfetLevel1Params& params) noexcept {
    if (vgs <= params.Vth - params.Vsmooth) {
      return -1; // Deep cutoff
    }
    if (vgs < params.Vth) {
      return 0; // Subthreshold smoothing
    }
    const double VGST = vgs - params.Vth;
    return (vds < VGST) ? 1 : 2; // Linear : Saturation
  }
};

} // namespace sim::electronics::devices::nonlinear

#endif // APEX_MOSFETLEVEL1_HPP
