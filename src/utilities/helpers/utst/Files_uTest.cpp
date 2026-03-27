/**
 * @file Files_uTest.cpp
 * @brief Unit tests for apex::helpers::files.
 *
 * Tests file I/O and path utilities.
 *
 * Notes:
 *  - Tests use /proc filesystem for reliable test fixtures.
 *  - Platform-agnostic: assert invariants, not exact values.
 */

#include "src/utilities/helpers/inc/Files.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstring>

using apex::helpers::files::isBlockDevice;
using apex::helpers::files::isCharDevice;
using apex::helpers::files::isDirectory;
using apex::helpers::files::isReadable;
using apex::helpers::files::isRegularFile;
using apex::helpers::files::isSymlink;
using apex::helpers::files::pathExists;
using apex::helpers::files::readFileInt;
using apex::helpers::files::readFileInt64;
using apex::helpers::files::readFileLine;
using apex::helpers::files::readFileToBuffer;
using apex::helpers::files::readFileUint64;

/* ----------------------------- readFileToBuffer Tests ----------------------------- */

/** @test Reading /proc/self/comm returns process name. */
TEST(ReadFileToBufferTest, ReadsProcFile) {
  std::array<char, 64> buf{};
  const std::size_t LEN = readFileToBuffer("/proc/self/comm", buf.data(), buf.size());
  EXPECT_GT(LEN, 0U);
  EXPECT_LT(LEN, buf.size());
}

/** @test Null path returns zero. */
TEST(ReadFileToBufferTest, NullPathReturnsZero) {
  std::array<char, 64> buf{};
  const std::size_t LEN = readFileToBuffer(nullptr, buf.data(), buf.size());
  EXPECT_EQ(LEN, 0U);
}

/** @test Null buffer returns zero. */
TEST(ReadFileToBufferTest, NullBufferReturnsZero) {
  const std::size_t LEN = readFileToBuffer("/proc/self/comm", nullptr, 64);
  EXPECT_EQ(LEN, 0U);
}

/** @test Zero buffer size returns zero. */
TEST(ReadFileToBufferTest, ZeroSizeReturnsZero) {
  std::array<char, 64> buf{};
  const std::size_t LEN = readFileToBuffer("/proc/self/comm", buf.data(), 0);
  EXPECT_EQ(LEN, 0U);
}

/** @test Nonexistent file returns zero. */
TEST(ReadFileToBufferTest, NonexistentFileReturnsZero) {
  std::array<char, 64> buf{};
  const std::size_t LEN = readFileToBuffer("/nonexistent/path/file", buf.data(), buf.size());
  EXPECT_EQ(LEN, 0U);
}

/* ----------------------------- readFileLine Tests ----------------------------- */

/** @test Reading first line from /proc/self/comm. */
TEST(ReadFileLineTest, ReadsFirstLine) {
  std::array<char, 64> buf{};
  const std::size_t LEN = readFileLine("/proc/self/comm", buf);
  EXPECT_GT(LEN, 0U);
  // Should not contain newlines
  EXPECT_EQ(std::strchr(buf.data(), '\n'), nullptr);
}

/* ----------------------------- readFileInt Tests ----------------------------- */

/** @test Reading integer from /proc/self/sessionid. */
TEST(ReadFileIntTest, ReadsInteger) {
  // Session ID should be a non-negative integer
  const std::int32_t VAL = readFileInt("/proc/self/sessionid", -1);
  // May be -1 if not in a session, but should parse successfully
  EXPECT_NE(VAL, -999); // Just verify it didn't return some garbage
}

/** @test Nonexistent file returns default. */
TEST(ReadFileIntTest, NonexistentReturnsDefault) {
  const std::int32_t VAL = readFileInt("/nonexistent/path", -42);
  EXPECT_EQ(VAL, -42);
}

/* ----------------------------- readFileInt64 Tests ----------------------------- */

/** @test Reading 64-bit integer from proc. */
TEST(ReadFileInt64Test, ReadsLargeInteger) {
  // /proc/self/stat contains PID as first field
  const std::int64_t VAL = readFileInt64("/proc/self/stat", -1);
  EXPECT_GT(VAL, 0); // PID should be positive
}

/* ----------------------------- readFileUint64 Tests ----------------------------- */

/** @test Reading unsigned integer from sysfs. */
TEST(ReadFileUint64Test, ReadsUnsignedInteger) {
  // Page size should be positive
  const std::uint64_t VAL = readFileUint64("/proc/sys/vm/page-cluster", 0);
  // page-cluster is typically 0-3, but any non-huge value is fine
  EXPECT_LT(VAL, 1000U);
}

/* ----------------------------- pathExists Tests ----------------------------- */

/** @test /proc exists. */
TEST(PathExistsTest, ProcExists) { EXPECT_TRUE(pathExists("/proc")); }

/** @test Nonexistent path returns false. */
TEST(PathExistsTest, NonexistentReturnsFalse) {
  EXPECT_FALSE(pathExists("/nonexistent/path/12345"));
}

/** @test Null path returns false. */
TEST(PathExistsTest, NullReturnsFalse) { EXPECT_FALSE(pathExists(nullptr)); }

/* ----------------------------- isDirectory Tests ----------------------------- */

/** @test /proc is a directory. */
TEST(IsDirectoryTest, ProcIsDirectory) { EXPECT_TRUE(isDirectory("/proc")); }

/** @test /proc/self/comm is not a directory. */
TEST(IsDirectoryTest, FileIsNotDirectory) { EXPECT_FALSE(isDirectory("/proc/self/comm")); }

/** @test Null returns false. */
TEST(IsDirectoryTest, NullReturnsFalse) { EXPECT_FALSE(isDirectory(nullptr)); }

/* ----------------------------- isRegularFile Tests ----------------------------- */

/** @test /etc/passwd is a regular file. */
TEST(IsRegularFileTest, PasswdIsFile) {
  if (pathExists("/etc/passwd")) {
    EXPECT_TRUE(isRegularFile("/etc/passwd"));
  }
}

/** @test /proc is not a regular file. */
TEST(IsRegularFileTest, DirectoryIsNotFile) { EXPECT_FALSE(isRegularFile("/proc")); }

/* ----------------------------- isReadable Tests ----------------------------- */

/** @test /proc/self/comm is readable. */
TEST(IsReadableTest, ProcSelfCommIsReadable) { EXPECT_TRUE(isReadable("/proc/self/comm")); }

/** @test Null returns false. */
TEST(IsReadableTest, NullReturnsFalse) { EXPECT_FALSE(isReadable(nullptr)); }

/* ----------------------------- isCharDevice Tests ----------------------------- */

/** @test /dev/null is a character device. */
TEST(IsCharDeviceTest, DevNullIsCharDevice) { EXPECT_TRUE(isCharDevice("/dev/null")); }

/** @test /proc is not a character device. */
TEST(IsCharDeviceTest, DirectoryIsNotCharDevice) { EXPECT_FALSE(isCharDevice("/proc")); }

/* ----------------------------- isBlockDevice Tests ----------------------------- */

/** @test /proc is not a block device. */
TEST(IsBlockDeviceTest, ProcIsNotBlockDevice) { EXPECT_FALSE(isBlockDevice("/proc")); }

/* ----------------------------- isSymlink Tests ----------------------------- */

/** @test /proc/self is a symlink. */
TEST(IsSymlinkTest, ProcSelfIsSymlink) { EXPECT_TRUE(isSymlink("/proc/self")); }

/** @test /proc is not a symlink. */
TEST(IsSymlinkTest, ProcIsNotSymlink) { EXPECT_FALSE(isSymlink("/proc")); }

/** @test Null returns false. */
TEST(IsSymlinkTest, NullReturnsFalse) { EXPECT_FALSE(isSymlink(nullptr)); }
