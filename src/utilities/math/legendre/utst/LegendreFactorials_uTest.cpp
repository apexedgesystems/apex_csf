/**
 * @file LegendreFactorials_uTest.cpp
 * @brief Unit tests for factorial precomputation and lookup utilities.
 *
 * Notes:
 *  - Tests verify factorial table generation and extension.
 *  - Tests are platform-agnostic: assert invariants, not exact implementation details.
 */

#include "src/utilities/math/legendre/inc/Factorials.hpp"

#include <gtest/gtest.h>

#include <cstdint>

using apex::math::legendre::largeFactorial;
using apex::math::legendre::precomputedFactorials;

/* ----------------------------- API Tests ----------------------------- */

/**
 * @test PrecomputedFactorials
 * @brief Verifies factorial values from 0! to 10! are precomputed correctly.
 */
TEST(MathLegendreFactorials, PrecomputedFactorials) {
  auto factorials = precomputedFactorials(10);

  EXPECT_DOUBLE_EQ(factorials[0], 1.0);        // 0! = 1
  EXPECT_DOUBLE_EQ(factorials[1], 1.0);        // 1! = 1
  EXPECT_DOUBLE_EQ(factorials[2], 2.0);        // 2! = 2
  EXPECT_DOUBLE_EQ(factorials[3], 6.0);        // 3! = 6
  EXPECT_DOUBLE_EQ(factorials[4], 24.0);       // 4! = 24
  EXPECT_DOUBLE_EQ(factorials[5], 120.0);      // 5! = 120
  EXPECT_DOUBLE_EQ(factorials[10], 3628800.0); // 10! = 3628800
}

/**
 * @test LargeFactorialWithinPrecomputed
 * @brief Checks largeFactorial returns correct values within the precomputed range.
 */
TEST(MathLegendreFactorials, LargeFactorialWithinPrecomputed) {
  auto factorials = precomputedFactorials(10);

  EXPECT_DOUBLE_EQ(largeFactorial(0, factorials), 1.0);   // 0! = 1
  EXPECT_DOUBLE_EQ(largeFactorial(1, factorials), 1.0);   // 1! = 1
  EXPECT_DOUBLE_EQ(largeFactorial(2, factorials), 2.0);   // 2! = 2
  EXPECT_DOUBLE_EQ(largeFactorial(3, factorials), 6.0);   // 3! = 6
  EXPECT_DOUBLE_EQ(largeFactorial(4, factorials), 24.0);  // 4! = 24
  EXPECT_DOUBLE_EQ(largeFactorial(5, factorials), 120.0); // 5! = 120
  EXPECT_DOUBLE_EQ(largeFactorial(10, factorials),
                   3628800.0); // 10! = 3628800
}

/**
 * @test LargeFactorialBeyondPrecomputed
 * @brief Ensures largeFactorial correctly extends factorial computation beyond precomputed table.
 */
TEST(MathLegendreFactorials, LargeFactorialBeyondPrecomputed) {
  auto factorials = precomputedFactorials(10);

  EXPECT_DOUBLE_EQ(largeFactorial(12, factorials),
                   479001600.0); // 12! = 479001600
  EXPECT_DOUBLE_EQ(largeFactorial(15, factorials),
                   1307674368000.0); // 15! = 1307674368000
  EXPECT_DOUBLE_EQ(largeFactorial(20, factorials),
                   2432902008176640000.0); // 20! = 2432902008176640000
}

/**
 * @test FactorialZero
 * @brief Validates that largeFactorial(0) yields 1.
 */
TEST(MathLegendreFactorials, FactorialZero) {
  auto factorials = precomputedFactorials(10);
  EXPECT_DOUBLE_EQ(largeFactorial(0, factorials), 1.0);
}

/**
 * @test FactorialOne
 * @brief Validates that largeFactorial(1) yields 1.
 */
TEST(MathLegendreFactorials, FactorialOne) {
  auto factorials = precomputedFactorials(10);
  EXPECT_DOUBLE_EQ(largeFactorial(1, factorials), 1.0);
}

#if LEGENDRE_HAS_SPAN
/**
 * @test LargeFactorialSpanOverload
 * @brief Exercises the std::span overload for both in-range and extended values.
 */
TEST(MathLegendreFactorials, LargeFactorialSpanOverload) {
  auto vec = precomputedFactorials(10);
  std::span<const double> spanFact(vec);

  EXPECT_DOUBLE_EQ(largeFactorial(4, spanFact), 24.0);
  EXPECT_DOUBLE_EQ(largeFactorial(15, spanFact), 1307674368000.0);
}
#endif
