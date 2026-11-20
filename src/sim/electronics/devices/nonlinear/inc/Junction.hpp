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
 * The exponential itself overflows a double above an argument of about 709,
 * which a Newton iterate can reach before the limiter above has a previous
 * voltage to work from. Every junction model therefore evaluates its
 * exponential through limitedExp(): the true exponential up to EXP_LINEAR_FROM
 * and its tangent line beyond. Value and slope stay finite and consistent
 * with each other, so a stamp built from them holds no NaN. At the default
 * thermal voltage the tangent starts 2.08 V across a bare junction, where a
 * 1e-14 A junction would already carry 5e20 A; no operating point of a real
 * circuit lies beyond it.
 *
 * RT-safety: RT-safe (static functions, no allocations).
 * Thread-safety: Safe (stateless, pure functions).
 */

#include <algorithm>
#include <cmath>

namespace sim::electronics::devices::nonlinear {

/* ----------------------------- JunctionPoint ----------------------------- */

/// Current through a junction and its derivative with respect to the terminal voltage.
struct JunctionPoint {
  double current = 0.0;     ///< Current (A), positive forward.
  double conductance = 0.0; ///< dI/dV (S).
};

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
  /// Exponent above which limitedExp() continues along its tangent line.
  static constexpr double EXP_LINEAR_FROM = 80.0;

  /**
   * @brief Exponential that grows linearly above EXP_LINEAR_FROM.
   *
   * exp(x) for x <= EXP_LINEAR_FROM; exp(EXP_LINEAR_FROM) * (1 + x - EXP_LINEAR_FROM)
   * above. Continuous with a continuous first derivative.
   *
   * @param x Exponent (junction voltage over n*Vt).
   * @return Limited exponential.
   * @note RT-safe.
   */
  [[nodiscard]] static double limitedExp(double x) noexcept {
    if (x <= EXP_LINEAR_FROM) {
      return std::exp(x);
    }
    return std::exp(EXP_LINEAR_FROM) * (1.0 + x - EXP_LINEAR_FROM);
  }

  /**
   * @brief Derivative of limitedExp() with respect to its argument.
   * @param x Exponent (junction voltage over n*Vt).
   * @return exp(x) up to EXP_LINEAR_FROM, exp(EXP_LINEAR_FROM) above.
   * @note RT-safe.
   */
  [[nodiscard]] static double limitedExpSlope(double x) noexcept {
    return std::exp(std::min(x, EXP_LINEAR_FROM));
  }

  /**
   * @brief Ideal junction current Is * (exp(V / (n*Vt)) - 1), exponential limited.
   * @param v Junction voltage (V), positive forward.
   * @param is Saturation current (A).
   * @param nVt Ideality factor times thermal voltage (V).
   * @return Junction current (A).
   * @note RT-safe.
   */
  [[nodiscard]] static double current(double v, double is, double nVt) noexcept {
    return is * (limitedExp(v / nVt) - 1.0);
  }

  /**
   * @brief Derivative of current() with respect to the junction voltage.
   * @param v Junction voltage (V), positive forward.
   * @param is Saturation current (A).
   * @param nVt Ideality factor times thermal voltage (V).
   * @return Junction conductance (S).
   * @note RT-safe.
   */
  [[nodiscard]] static double conductance(double v, double is, double nVt) noexcept {
    return (is / nVt) * limitedExpSlope(v / nVt);
  }

  /// Iteration limit of withSeriesResistance(); it converges in under ten.
  static constexpr int SERIES_MAX_ITER = 60;

  /**
   * @brief Current and conductance of a junction behind a series resistance.
   *
   * The terminal voltage divides between the junction and the resistance:
   *   V = Vj + Rs * Is * (exp(Vj / (n*Vt)) - 1)
   * This is solved for the junction voltage Vj by Newton's method. The
   * left-hand side minus V is increasing and convex in Vj, so from a start at
   * or above the root the iterates descend onto it without overshoot. The
   * start is the smaller of V (all of it across the junction) and the
   * junction voltage that would pass V / Rs (all of it across the
   * resistance); both lie at or above the root for forward bias. In reverse
   * bias the junction is close to linear and one step lands on the root.
   *
   * @param v Terminal voltage (V), positive forward.
   * @param is Saturation current (A).
   * @param nVt Ideality factor times thermal voltage (V).
   * @param rs Series resistance (ohm); zero or negative means none.
   * @return Current, and dI/dV = gj / (1 + gj * Rs) at the solved point.
   * @note RT-safe: at most SERIES_MAX_ITER iterations, no allocation.
   */
  [[nodiscard]] static JunctionPoint withSeriesResistance(double v, double is, double nVt,
                                                          double rs) noexcept {
    if (rs <= 0.0) {
      return {current(v, is, nVt), conductance(v, is, nVt)};
    }

    double vj = v;
    if (v > 0.0) {
      vj = std::min(v, nVt * std::log1p(v / (rs * is)));
    }

    for (int iter = 0; iter < SERIES_MAX_ITER; ++iter) {
      const double RESIDUAL = vj + rs * current(vj, is, nVt) - v;
      const double SLOPE = 1.0 + rs * conductance(vj, is, nVt);
      const double STEP = RESIDUAL / SLOPE;
      vj -= STEP;
      if (std::abs(STEP) <= 1e-15 * (std::abs(vj) + nVt)) {
        break;
      }
    }

    const double GJ = conductance(vj, is, nVt);
    return {current(vj, is, nVt), GJ / (1.0 + GJ * rs)};
  }

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
