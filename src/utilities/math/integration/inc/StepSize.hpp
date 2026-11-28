#ifndef APEX_MATH_INTEGRATION_STEPSIZE_HPP
#define APEX_MATH_INTEGRATION_STEPSIZE_HPP
/**
 * @file StepSize.hpp
 * @brief The step-size test every integrator and accumulator applies before it advances.
 *
 * One definition, so no stepper accepts a step size another refuses.
 */

#include <limits>

namespace apex {
namespace math {
namespace integration {

/* ----------------------------------- API ---------------------------------- */

/**
 * @brief Whether a step of size dt can be taken: dt is positive and finite.
 *
 * Zero, negative, infinite and NaN values are refused. A NaN fails both
 * comparisons, which is why the test is written as what a valid step is
 * and not as what an invalid one is. Comparisons only, so the test can be
 * used in constant expressions under every language standard the library
 * builds with.
 *
 * @param dt Proposed step size.
 * @return True when dt > 0 and dt is finite.
 * @note RT-safe: Two comparisons, no allocation.
 */
[[nodiscard]] constexpr bool isValidStep(double dt) noexcept {
  return dt > 0.0 && dt <= std::numeric_limits<double>::max();
}

} // namespace integration
} // namespace math
} // namespace apex

#endif // APEX_MATH_INTEGRATION_STEPSIZE_HPP
