/**
 * @file containsString_uTest.cpp
 * @brief Unit tests for apex::helpers::strings::containsString.
 *
 * Coverage:
 *  - Target exists (positive case)
 *  - Target does not exist
 *  - Empty vector input
 *  - Case sensitivity (no lowercase match for capitalized entries)
 *  - Substring should not match (exact match required)
 */

#include "src/utilities/helpers/inc/Utilities.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

// Fixture ---------------------------------------------------------------------

class ContainsStringTest : public ::testing::Test {
protected:
  std::vector<std::string> vec_ = {"apple", "banana", "cherry"};
};

// -----------------------------------------------------------------------------

/**
 * @test Target exists in the vector.
 */
TEST_F(ContainsStringTest, TargetExists) {
  // Act & Assert
  EXPECT_TRUE(apex::helpers::strings::containsString(vec_, "banana"));
}

/**
 * @test Target does not exist in the vector.
 */
TEST_F(ContainsStringTest, TargetDoesNotExist) {
  // Act & Assert
  EXPECT_FALSE(apex::helpers::strings::containsString(vec_, "orange"));
}

/**
 * @test Empty vector returns false.
 */
TEST_F(ContainsStringTest, EmptyVector) {
  // Arrange
  std::vector<std::string> emptyVec;

  // Act & Assert
  EXPECT_FALSE(apex::helpers::strings::containsString(emptyVec, "banana"));
}

/**
 * @test Case-sensitive comparison: lowercase target should not match capitalized entries.
 */
TEST_F(ContainsStringTest, CaseSensitivity) {
  // Arrange
  std::vector<std::string> caseVec = {"Apple", "Banana", "Cherry"};

  // Act & Assert
  EXPECT_FALSE(apex::helpers::strings::containsString(caseVec, "apple"));
}

/**
 * @test Substrings do not count as matches (requires exact equality).
 */
TEST_F(ContainsStringTest, SubstringNotMatch) {
  // Act & Assert
  EXPECT_FALSE(apex::helpers::strings::containsString(vec_, "ban"));
}
