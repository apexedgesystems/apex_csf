/**
 * @file checkFileExists_uTest.cpp
 * @brief Unit tests for apex::helpers::files::checkFileExists.
 *
 * Coverage:
 *  - Existing regular file returns true
 *  - Non-existent path returns false
 *  - Empty path returns false
 *  - Directory path returns true (exists on filesystem)
 */

#include "src/utilities/helpers/inc/Utilities.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdio>  // std::remove
#include <fstream> // std::ofstream
#include <string>  // std::string

// Fixture ---------------------------------------------------------------------

class CheckFileExistsTest : public ::testing::Test {
protected:
  std::string testFile_ = "test_file.txt";

  void SetUp() override {
    // Arrange: create a small file for existence checks
    std::ofstream ofs(testFile_);
    ofs << "Sample content";
    // ofs dtor closes the file
  }

  void TearDown() override {
    // Cleanup: remove file if present (ignore result)
    (void)std::remove(testFile_.c_str());
  }
};

// -----------------------------------------------------------------------------

/**
 * @test Existing file path returns true.
 */
TEST_F(CheckFileExistsTest, FileExists) {
  // Act & Assert
  EXPECT_TRUE(apex::helpers::files::checkFileExists(testFile_));
}

/**
 * @test Non-existent path returns false.
 */
TEST_F(CheckFileExistsTest, FileDoesNotExist) {
  // Arrange
  const std::string NON_EXISTENT_FILE = "non_existent.txt";

  // Act & Assert
  EXPECT_FALSE(apex::helpers::files::checkFileExists(NON_EXISTENT_FILE));
}

/**
 * @test Empty path returns false.
 */
TEST(CheckFileExistsStandalone, EmptyPath) {
  EXPECT_FALSE(apex::helpers::files::checkFileExists(""));
}

/**
 * @test Directory path exists (e.g., current directory ".").
 */
TEST(CheckFileExistsStandalone, DirectoryInsteadOfFile) {
  EXPECT_TRUE(apex::helpers::files::checkFileExists("."));
}
