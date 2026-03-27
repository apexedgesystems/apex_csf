#ifndef APEX_MATH_INTEGRATION_SDIRK2_HPP
#define APEX_MATH_INTEGRATION_SDIRK2_HPP
/**
 * @file SDIRK2.hpp
 * @brief 2-stage SDIRK (Diagonally Implicit Runge-Kutta) integrator.
 *
 * L-stable second-order implicit method. Good for DAEs and very stiff systems.
 * @note RT-safe with RT-safe callbacks (ImplicitOptionsRT).
 */

#include "src/utilities/math/integration/inc/Integration.hpp"
#include "src/utilities/math/integration/inc/IntegrationOptions.hpp"

#include <functional>

namespace apex {
namespace math {
namespace integration {

/* --------------------------------- SDIRK2 --------------------------------- */

/**
 * @brief 2-stage SDIRK (Diagonally Implicit Runge-Kutta) integrator.
 *
 * L-stable, two implicit solves per step.
 * Butcher tableau for gamma = 1 - 1/sqrt(2):
 *
 *    gamma   | gamma
 *  1         | 1-gamma    gamma
 *  ---------------------------
 *              1-gamma    gamma
 *
 *    c1 = gamma,      a11 = gamma
 *    c2 = 1,          a21 = 1-gamma,  a22 = gamma
 *    b1 = 1-gamma,    b2 = gamma
 *
 * @tparam State   Must support +=, -, * double.
 * @tparam Options Must be SDIRK2Options<State>.
 */
template <typename State, typename Options = SDIRK2Options<State>>
class SDIRK2 : public IntegratorBase<SDIRK2<State, Options>, State, Options> {
public:
  using Base = IntegratorBase<SDIRK2<State, Options>, State, Options>;
  using Func = std::function<State(const State&, double)>;

  SDIRK2() = default;
  ~SDIRK2() = default;

  /**
   * @brief Bind derivative functor and reset stats/time.
   */
  uint8_t initializeImpl(Func f, const State& initialState, double t0,
                         const Options& opts) noexcept;

  /**
   * @brief Perform one fixed-step SDIRK2 step.
   */
  uint8_t stepImpl(State& state, double t, double dt, const Options& opts) noexcept;

private:
  Func f_;

  static constexpr double SQRT2 = 1.4142135623730950488;
  static constexpr double GAMMA = 1.0 - 1.0 / SQRT2;
};

} // namespace integration
} // namespace math
} // namespace apex

#include "src/utilities/math/integration/src/SDIRK2.tpp"
#endif // APEX_MATH_INTEGRATION_SDIRK2_HPP
