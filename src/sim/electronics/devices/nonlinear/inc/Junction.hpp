#ifndef APEX_JUNCTION_HPP
#define APEX_JUNCTION_HPP
/**
 * @file Junction.hpp
 * @brief Numerical helpers shared by the p-n junction models (diodes, BJT
 *        junctions, JFET gate).
 *
 * A junction current grows as exp(V / (n*Vt)). Newton-Raphson on a circuit
 * with a junction starts from a linearization with almost no conductance, so
 * its first step puts most of the supply across the junction. From there the
 * linearization is so steep that each further step lowers the junction
 * voltage by only about n*Vt, and a solve needs (V_supply / n*Vt) iterations
 * to come back down. Limiting the junction-voltage step between iterations
 * removes that: the iterate climbs the exponential in steps the model can
 * follow and converges in a handful of iterations.
 *
 * RT-safety: RT-safe (static functions, no allocations).
 * Thread-safety: Safe (stateless, pure functions).
 */

#include <cmath>

namespace sim::electronics::devices::nonlinear {

/* ----------------------------- Junction ----------------------------- */

/**
 * @brief Junction-voltage helpers for Newton-Raphson iteration.
 *
 * Usage:
 * @code
 * const double N_VT = params.n * params.Vt;
 * const double V_CRIT = Junction::criticalVoltage(params.Is, N_VT);
 *
 * double vOld = 0.0;
 * for (...) {
 *   const double V_SOLVED = solveLinearizedAt(vOld);
 *   vOld = Junction::limitVoltage(V_SOLVED, vOld, N_VT, V_CRIT);
 * }
 * @endcode
 */
struct Junction {
  /**
   * @brief Voltage above which a junction's step is limited.
   *
   * The point of maximum curvature of the junction I-V curve:
   *   Vcrit = n*Vt * ln(n*Vt / (sqrt(2) * Is))
   *
   * @param is Junction saturation current (A).
   * @param nVt Ideality factor times thermal voltage (V).
   * @return Critical voltage (V).
   * @note RT-safe.
   */
  [[nodiscard]] static double criticalVoltage(double is, double nVt) noexcept {
    return nVt * std::log(nVt / (std::sqrt(2.0) * is));
  }

  /**
   * @brief Limit the change of a junction voltage between two iterations.
   *
   * A proposed voltage is accepted when it is below the critical voltage or
   * within two thermal voltages of the previous one. Otherwise the step is
   * taken in current instead of voltage: the new voltage is the one whose
   * exponential current matches the linear extrapolation of the old
   * operating point, which grows only logarithmically with the proposed
   * step.
   *
   * @param vNew Junction voltage proposed by the linear solve (V).
   * @param vOld Junction voltage of the previous iteration (V).
   * @param nVt Ideality factor times thermal voltage (V).
   * @param vCrit Critical voltage from criticalVoltage() (V).
   * @return The junction voltage to linearize at next (V).
   * @note RT-safe.
   */
  [[nodiscard]] static double limitVoltage(double vNew, double vOld, double nVt,
                                           double vCrit) noexcept {
    if (vNew <= vCrit || std::abs(vNew - vOld) <= 2.0 * nVt) {
      return vNew;
    }
    if (vOld > 0.0) {
      const double ARG = 1.0 + (vNew - vOld) / nVt;
      return (ARG > 0.0) ? vOld + nVt * std::log(ARG) : vCrit;
    }
    return nVt * std::log(vNew / nVt);
  }
};

} // namespace sim::electronics::devices::nonlinear

#endif // APEX_JUNCTION_HPP
