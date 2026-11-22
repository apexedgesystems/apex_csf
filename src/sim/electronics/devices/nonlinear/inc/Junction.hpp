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
 * and its tangent line beyond. Value and slope are consistent with each
 * other, and with positive finite parameters both stay finite at the
 * junction voltages a solve passes through: tens and hundreds of volts
 * either way (the unit tests check +/-20 V and +/-100 V). This is not a
 * guarantee for every finite input. The tangent itself overflows for an
 * argument near 3e273, and no function here checks that a saturation current
 * or a thermal voltage is positive, with the one exception of
 * seriesJunctionVoltage().
 *
 * Beyond the tangent point the value is the continuation and not the
 * junction's: at the default thermal voltage that is 2.08 V across a bare
 * junction, where the exponential would give a 1e-14 A junction 5e20 A.
 *
 * RT-safety: every function is static, allocates nothing and does a bounded
 * amount of work. seriesJunctionVoltage() and withSeriesResistance() iterate,
 * up to SERIES_MAX_ITER times, and how many depends on their inputs.
 * Thread-safety: Safe (stateless, pure functions).
 */

#include <algorithm>
#include <cmath>
#include <limits>

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

  /// Iteration limit of seriesJunctionVoltage().
  static constexpr int SERIES_MAX_ITER = 60;

  /// A junction voltage found by seriesJunctionVoltage(), and how.
  struct SeriesSolve {
    double junctionVolts = 0.0; ///< Voltage across the junction itself (V).
    int iterations = 0;         ///< Evaluations of the terminal equation.
    bool converged = false;     ///< Whether the solve met its tolerance.
  };

  /**
   * @brief Junction voltage of a junction behind a series resistance.
   *
   * The terminal voltage divides between the junction and the resistance:
   *   V = Vj + Rs * Is * (exp(Vj / (n*Vt)) - 1)
   * The left-hand side minus V is increasing and convex in Vj, and its root
   * lies between 0 and V: the junction takes part of the terminal voltage,
   * in the terminal voltage's sense. Newton's method from a start at or
   * above the root descends onto it without overshoot; from below, its
   * first step can land far up the exponential, from where each further
   * step comes down by only about n*Vt. The start is therefore always an
   * upper one:
   *   - forward, the smaller of V (all of it across the junction) and the
   *     junction voltage that would pass V / Rs (all of it across the
   *     resistance);
   *   - reverse, 0 V.
   *
   * The solve has converged when the terminal equation is met to within
   * 1e-15 of the sizes of its three terms, which is what a double resolves
   * of their sum, or when a step is below 1e-15 of |Vj| + n*Vt, which is
   * what it resolves of Vj. It stops there, or at SERIES_MAX_ITER
   * evaluations, and says which.
   *
   * The argument above holds for a finite voltage and positive finite
   * parameters. A saturation current or thermal voltage that is not
   * positive is refused before any evaluation; a value that is not finite
   * runs to the limit. Neither is reported as converged.
   *
   * @param v Terminal voltage (V), positive forward. Finite.
   * @param is Saturation current (A). Positive and finite.
   * @param nVt Ideality factor times thermal voltage (V). Positive and finite.
   * @param rs Series resistance (ohm). Positive and finite.
   * @return The junction voltage, the evaluations taken and whether they converged.
   * @note RT-safe: at most SERIES_MAX_ITER iterations, no allocation.
   */
  [[nodiscard]] static SeriesSolve seriesJunctionVoltage(double v, double is, double nVt,
                                                         double rs) noexcept {
    if (!(is > 0.0 && nVt > 0.0)) {
      return {0.0, 0, false};
    }
    double vj = v > 0.0 ? std::min(v, nVt * std::log1p(v / (rs * is))) : 0.0;
    for (int iter = 1; iter <= SERIES_MAX_ITER; ++iter) {
      const double DROP = rs * current(vj, is, nVt);
      const double RESIDUAL = vj + DROP - v;
      if (std::abs(RESIDUAL) <= 1e-15 * (std::abs(vj) + std::abs(DROP) + std::abs(v))) {
        return {vj, iter, true};
      }
      const double STEP = RESIDUAL / (1.0 + rs * conductance(vj, is, nVt));
      vj -= STEP;
      if (std::abs(STEP) <= 1e-15 * (std::abs(vj) + nVt)) {
        return {vj, iter, true};
      }
    }
    return {vj, SERIES_MAX_ITER, false};
  }

  /**
   * @brief Current and conductance of a junction behind a series resistance.
   *
   * The junction voltage is found by seriesJunctionVoltage(); the current
   * is the junction's at that voltage, and the conductance follows from
   * differentiating the terminal equation.
   *
   * A solve that did not converge returns no number: both values are NaN.
   * A finite voltage with positive finite parameters converges; a
   * saturation current or thermal voltage that is not positive, or a value
   * that is not finite, is what does not.
   *
   * @param v Terminal voltage (V), positive forward.
   * @param is Saturation current (A), positive.
   * @param nVt Ideality factor times thermal voltage (V), positive.
   * @param rs Series resistance (ohm); zero or negative means none.
   * @return Current, and dI/dV = gj / (1 + gj * Rs) at the solved point; NaN
   *         for both if the solve did not converge.
   * @note RT-safe: at most SERIES_MAX_ITER iterations, no allocation.
   */
  [[nodiscard]] static JunctionPoint withSeriesResistance(double v, double is, double nVt,
                                                          double rs) noexcept {
    if (rs <= 0.0) {
      return {current(v, is, nVt), conductance(v, is, nVt)};
    }
    const SeriesSolve SOLVED = seriesJunctionVoltage(v, is, nVt, rs);
    if (!SOLVED.converged) {
      const double NONE = std::numeric_limits<double>::quiet_NaN();
      return {NONE, NONE};
    }
    const double GJ = conductance(SOLVED.junctionVolts, is, nVt);
    return {current(SOLVED.junctionVolts, is, nVt), GJ / (1.0 + GJ * rs)};
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
