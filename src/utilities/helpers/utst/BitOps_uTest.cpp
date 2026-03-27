/**
 * @file BitOps_uTest.cpp
 * @brief Unit tests for apex::helpers::bit_ops (bitSet/bitClear/bitFlip/bitCheck).
 *
 * Coverage:
 *  - Set/Clear/Flip on specific bits
 *  - bitCheck true/false cases
 *  - Idempotence / toggle behavior
 *  - Out-of-range indices are masked to [0,7] (no UB)
 *  - Other bits remain unchanged
 */

#include "src/utilities/helpers/inc/Bits.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>

using ::testing::Eq;

namespace { // Helpers ---------------------------------------------------------

// Return a byte with only bit 'idx' set (idx masked to [0,7]).
std::uint8_t maskBit(std::uint8_t idx) {
  const unsigned MASKED = static_cast<unsigned>(idx) & 7u;
  return static_cast<std::uint8_t>(1u << MASKED);
}

} // namespace

// Fixture ---------------------------------------------------------------------

class BitOpsTest : public ::testing::Test {};

// -----------------------------------------------------------------------------

/**
 * @test bitSet sets only the requested bit.
 */
TEST_F(BitOpsTest, BitSet_SetsRequestedBit) {
  // Arrange
  std::uint8_t b = 0x00u;
  const std::uint8_t EXPECTED = 0x08u; // bit 3

  // Act
  apex::helpers::bits::set(b, 3u);

  // Assert
  EXPECT_THAT(b, Eq(EXPECTED));
}

/**
 * @test bitClear clears only the requested bit.
 */
TEST_F(BitOpsTest, BitClear_ClearsRequestedBit) {
  // Arrange
  std::uint8_t b = 0xFFu;
  const std::uint8_t EXPECTED = 0x7Fu; // clear bit 7

  // Act
  apex::helpers::bits::clear(b, 7u);

  // Assert
  EXPECT_THAT(b, Eq(EXPECTED));
}

/**
 * @test bitFlip toggles the requested bit and leaves others unchanged.
 */
TEST_F(BitOpsTest, BitFlip_TogglesBit) {
  // Arrange
  std::uint8_t b = 0x10u; // 0001 0000 (bit 4 set)
  const std::uint8_t EXPECTED_OFF = 0x00u;
  const std::uint8_t EXPECTED_ON = 0x10u;

  // Act & Assert
  apex::helpers::bits::flip(b, 4u); // turn off
  EXPECT_THAT(b, Eq(EXPECTED_OFF));

  apex::helpers::bits::flip(b, 4u); // turn on again
  EXPECT_THAT(b, Eq(EXPECTED_ON));
}

/**
 * @test bitCheck reports true only for the requested set bit.
 */
TEST_F(BitOpsTest, BitCheck_ReportsPresence) {
  // Arrange
  const std::uint8_t B = 0b1010'0001u; // bits 0,5,7 set

  // Act & Assert
  EXPECT_TRUE(apex::helpers::bits::test(B, 0u));
  EXPECT_FALSE(apex::helpers::bits::test(B, 1u));
  EXPECT_FALSE(apex::helpers::bits::test(B, 2u));
  EXPECT_FALSE(apex::helpers::bits::test(B, 3u));
  EXPECT_FALSE(apex::helpers::bits::test(B, 4u));
  EXPECT_TRUE(apex::helpers::bits::test(B, 5u));
  EXPECT_FALSE(apex::helpers::bits::test(B, 6u));
  EXPECT_TRUE(apex::helpers::bits::test(B, 7u));
}

/**
 * @test Out-of-range indices are masked to [0,7] safely (no UB).
 */
TEST_F(BitOpsTest, IndicesMasked_NoUndefinedBehavior) {
  // Arrange
  std::uint8_t b = 0x00u;

  // Act: set with 9 -> same as setting bit 1
  apex::helpers::bits::set(b, 9u);
  const std::uint8_t EXPECTED_AFTER_SET = maskBit(1u);
  EXPECT_THAT(b, Eq(EXPECTED_AFTER_SET));

  // Act: flip with 15 -> same as flipping bit 7
  apex::helpers::bits::flip(b, 15u);
  const std::uint8_t EXPECTED_AFTER_FLIP =
      static_cast<std::uint8_t>(EXPECTED_AFTER_SET ^ maskBit(7u));
  EXPECT_THAT(b, Eq(EXPECTED_AFTER_FLIP));

  // Act: clear with 16 -> same as clearing bit 0
  apex::helpers::bits::set(b, 0u); // ensure bit 0 is set before clearing
  apex::helpers::bits::clear(b, 16u);
  const std::uint8_t EXPECTED_AFTER_CLEAR =
      static_cast<std::uint8_t>((EXPECTED_AFTER_FLIP | maskBit(0u)) & ~maskBit(0u));
  EXPECT_THAT(b, Eq(EXPECTED_AFTER_CLEAR));

  // Act: check with 24 -> same as checking bit 0
  const bool CHECK_MASKED = apex::helpers::bits::test(b, 24u);
  const bool EXPECTED_CHECK = apex::helpers::bits::test(b, 0u);
  EXPECT_EQ(CHECK_MASKED, EXPECTED_CHECK);
}

/**
 * @test Other bits remain unchanged when operating on a specific bit.
 */
TEST_F(BitOpsTest, OtherBitsRemainUnchanged) {
  // Arrange
  const std::uint8_t INITIAL = 0b0101'0101u; // alternating bits
  std::uint8_t b = INITIAL;

  // Act
  apex::helpers::bits::set(b, 2u);   // bit 2 already set; no change
  apex::helpers::bits::clear(b, 6u); // clear bit 6 (was 1 -> becomes 0)
  apex::helpers::bits::flip(b, 4u);  // flip bit 4

  // Assert
  // Expected: starting 0101 0101
  // - clear bit 6 => 0001 0101
  // - flip bit 4  => 0000 0101
  const std::uint8_t EXPECTED = 0b0000'0101u;
  EXPECT_THAT(b, Eq(EXPECTED));
}
