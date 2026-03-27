/**
 * @file hex2struct_uTest.cpp
 * @brief Unit tests for apex::helpers::files::hex2cpp (raw binary → struct).
 *
 * Coverage:
 *  - Success: exact-size file populates trivially copyable struct
 *  - Size mismatch (smaller file) → returns false & sets error
 *  - Nonexistent path → returns false & sets error
 *  - Empty path → returns false & sets error "Empty path"
 *
 * Notes:
 *  - Files are written with std::ofstream only in tests (runtime impl uses POSIX).
 *  - Endianness: tests validate raw host representation (no swapping expected here).
 */

#include "src/utilities/helpers/inc/Utilities.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>  // std::remove
#include <fstream> // std::ofstream
#include <random>
#include <string> // std::string
#include <type_traits>

using ::testing::HasSubstr;

namespace { // Helpers ---------------------------------------------------------

template <typename T> void writeRawObject(const std::string& path, const T& obj) {
  static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
  std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
  ofs.write(reinterpret_cast<const char*>(&obj), static_cast<std::streamsize>(sizeof(T)));
}

void writeZeroBytes(const std::string& path, std::size_t n) {
  std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
  std::string zeros(n, '\0');
  ofs.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
}

} // namespace

// Fixture ---------------------------------------------------------------------

class Hex2StructTest : public ::testing::Test {
protected:
  std::string testFile_ = [] {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    return "hex2cpp_tmp_" + std::to_string(dist(gen)) + ".bin";
  }();

  void TearDown() override {
    (void)std::remove(testFile_.c_str()); // best-effort cleanup
  }
};

// -----------------------------------------------------------------------------

/**
 * @test Success: exact-size file populates the struct correctly.
 */
TEST_F(Hex2StructTest, Success_ExactSize) {
  // Arrange
  struct Sample {
    std::uint32_t a;
    std::uint16_t b;
  };
  const Sample SRC{0x11223344u, 0x5566u};
  writeRawObject(testFile_, SRC);

  Sample dst{};
  std::string err;

  // Act
  const bool OK = apex::helpers::files::hex2cpp(testFile_, dst, std::ref(err));

  // Assert
  ASSERT_TRUE(OK) << err;
  EXPECT_EQ(dst.a, SRC.a);
  EXPECT_EQ(dst.b, SRC.b);
  EXPECT_TRUE(err.empty());
}

/**
 * @test Size mismatch (file too small) returns false and sets an error.
 */
TEST_F(Hex2StructTest, SizeMismatch_SmallerFile) {
  // Arrange
  struct Sample {
    std::uint32_t a;
    std::uint16_t b;
  };
  Sample dst{};
  std::string err;

  writeZeroBytes(testFile_, /*n=*/3U); // smaller than sizeof(Sample)

  // Act
  const bool OK = apex::helpers::files::hex2cpp(testFile_, dst, std::ref(err));

  // Assert
  ASSERT_FALSE(OK);
  EXPECT_THAT(err, HasSubstr("size mismatch"));
}

/**
 * @test Nonexistent path returns false and sets an error.
 */
TEST_F(Hex2StructTest, NonexistentPath) {
  // Arrange
  const std::string PATH = "definitely_not_here_123456.bin";
  std::string err;
  std::uint32_t dst{}; // destination must be mutable (function writes into it)

  // Act
  const bool OK = apex::helpers::files::hex2cpp(PATH, dst, std::ref(err));

  // Assert
  ASSERT_FALSE(OK);
  EXPECT_THAT(err, HasSubstr("open failed"));
}

/**
 * @test Empty path returns false and sets 'Empty path'.
 */
TEST_F(Hex2StructTest, EmptyPath) {
  // Arrange
  std::string err;
  std::uint32_t dummy{};

  // Act
  const bool OK = apex::helpers::files::hex2cpp(std::string{}, dummy, std::ref(err));

  // Assert
  ASSERT_FALSE(OK);
  EXPECT_THAT(err, HasSubstr("Empty path"));
}
