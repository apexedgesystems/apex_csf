#ifndef APEX_MATH_INTEGRATION_UTST_SUPPORT_CONVERGENCEORDER_HPP
#define APEX_MATH_INTEGRATION_UTST_SUPPORT_CONVERGENCEORDER_HPP
/**
 * @file ConvergenceOrder.hpp
 * @brief Measured order of an integrator: how fast its error against an
 *        analytic solution falls when the step is halved. A method of order p
 *        has an error proportional to dt^p, so halving dt divides the error
 *        by 2^p. A wrong coefficient, a missing term or a poor start step
 *        shows as a lower order, which a tolerance on one result cannot tell
 *        from a merely larger error.
 */

#include <cmath>

namespace apex {
namespace math {
namespace integration {
namespace test {

/**
 * @brief Order observed between a run of `steps` steps and one of twice as many.
 * @param errorAt Callable double(int steps): the error at the end of a fixed
 *                interval covered in that many equal steps.
 * @param steps   Step count of the coarser run.
 */
template <typename ErrorAt> double observedOrder(ErrorAt&& errorAt, int steps) {
  return std::log2(errorAt(steps) / errorAt(2 * steps));
}

} // namespace test
} // namespace integration
} // namespace math
} // namespace apex

#endif // APEX_MATH_INTEGRATION_UTST_SUPPORT_CONVERGENCEORDER_HPP
