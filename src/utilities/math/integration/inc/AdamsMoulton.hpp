#ifndef APEX_MATH_INTEGRATION_ADAMSMOULTON_HPP
#define APEX_MATH_INTEGRATION_ADAMSMOULTON_HPP
/**
 * @file AdamsMoulton.hpp
 * @brief 2-step Adams-Moulton predictor-corrector (implicit multi-step).
 *
 * High-accuracy implicit method using Adams-Bashforth predictor.
 * @note RT-safe with RT-safe callbacks (ImplicitOptionsRT).
 */

#include "src/utilities/math/integration/inc/Integration.hpp"
#include "src/utilities/math/integration/inc/IntegrationOptions.hpp"

#include <functional>

namespace apex {
namespace math {
namespace integration {

/* ----------------------------- AdamsMoulton ------------------------------- */

/**
 * @brief 2-step Adams-Moulton predictor-corrector (implicit multi-step).
 *
 * Predictor:  x_pred = x_n + dt*(1.5 f_n - 0.5 f_{n-1})
 * Corrector: solve
 *   x_{n+1} = x_n + (dt/12)[5 f(x_{n+1},t_{n+1}) + 8 f_n - f_{n-1}]
 *
 * @tparam State   Must support +=, -, * double.
 * @tparam Options Must be AdamsMoultonOptions<State>.
 */
template <typename State, typename Options = AdamsMoultonOptions<State>>
class AdamsMoulton : public IntegratorBase<AdamsMoulton<State, Options>, State, Options> {
public:
  using Base = IntegratorBase<AdamsMoulton<State, Options>, State, Options>;
  using Func = std::function<State(const State&, double)>;

  AdamsMoulton() = default;
  ~AdamsMoulton() = default;

  /**
   * @brief Bind derivative functor and seed history with initialState.
   */
  uint8_t initializeImpl(Func f, const State& initialState, double t0,
                         const Options& opts) noexcept;

  /**
   * @brief One predictor-corrector step via Newton-Raphson.
   */
  uint8_t stepImpl(State& state, double t, double dt, const Options& opts) noexcept;

private:
  Func f_;
  State xNm1_;
  State xNm2_;
};

} // namespace integration
} // namespace math
} // namespace apex

#include "src/utilities/math/integration/src/AdamsMoulton.tpp"
#endif // APEX_MATH_INTEGRATION_ADAMSMOULTON_HPP
