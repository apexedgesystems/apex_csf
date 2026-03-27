/**
 * @file printSpan_uTest.cpp
 * @brief Unit tests for apex::helpers::format::printSpan.
 *
 * Coverage:
 *  - Empty span prints nothing
 *  - Single byte prints as "0xab \n"
 *  - Multiple bytes print space-separated hex with trailing space and newline
 */

#include "src/utilities/helpers/inc/Utilities.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

// Fixture ---------------------------------------------------------------------

class PrintSpanTest : public ::testing::Test {};

// -----------------------------------------------------------------------------

/**
 * @test Empty span produces no output.
 */
TEST_F(PrintSpanTest, EmptySpan) {
  // Arrange
  std::vector<std::uint8_t> emptySpan;

  // Act
  testing::internal::CaptureStdout();
  apex::helpers::format::printSpan(emptySpan);
  std::string output = testing::internal::GetCapturedStdout();

  // Assert
  EXPECT_EQ(output, "");
}

/**
 * @test Single byte prints expected hex with trailing space and newline.
 */
TEST_F(PrintSpanTest, SingleByte) {
  // Arrange
  std::vector<std::uint8_t> span = {0xAB};

  // Act
  testing::internal::CaptureStdout();
  apex::helpers::format::printSpan(span);
  std::string output = testing::internal::GetCapturedStdout();

  // Assert
  EXPECT_EQ(output, "0xab \n");
}

/**
 * @test Multiple bytes print space-separated hex with trailing space and newline.
 */
TEST_F(PrintSpanTest, MultipleBytes) {
  // Arrange
  std::vector<std::uint8_t> span = {0x12, 0x34, 0x56};

  // Act
  testing::internal::CaptureStdout();
  apex::helpers::format::printSpan(span);
  std::string output = testing::internal::GetCapturedStdout();

  // Assert
  EXPECT_EQ(output, "0x12 0x34 0x56 \n");
}
