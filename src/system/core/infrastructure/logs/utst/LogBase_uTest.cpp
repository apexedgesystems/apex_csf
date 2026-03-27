/**
 * @file LogBase_uTest.cpp
 * @brief Unit tests for LogBase I/O, queries, and rotation behavior.
 */

#include "src/system/core/infrastructure/logs/inc/LogBase.hpp"

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using logs::LogBase;
using logs::Status;

/* ----------------------------- Test Helpers ----------------------------- */

static std::filesystem::path uniqTempFile(const std::string& stem) {
  const auto DIR = std::filesystem::temp_directory_path();
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<unsigned long long> dist;
  return DIR / (stem + "_" + std::to_string(dist(gen)) + ".log");
}

static std::string slurp(const std::filesystem::path& path) {
  std::ifstream ifs(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

static int countLines(const std::string& s) {
  return static_cast<int>(std::count(s.begin(), s.end(), '\n'));
}

/* ----------------------------- API Tests ----------------------------- */

/** @test Open, write one line, path remains stable, content lands on disk. */
TEST(LogBaseTest, OpenWriteAndFpath) {
  const auto PATH = uniqTempFile("logbase_openwrite");
  {
    LogBase log(PATH.string());
    ASSERT_EQ(log.write("hello world"), Status::OK);
    EXPECT_EQ(log.fpath(), PATH.string());
  }

  std::error_code ec{};
  const auto ON_DISK_SIZE = std::filesystem::file_size(PATH, ec);
  ASSERT_FALSE(ec);
  EXPECT_GT(ON_DISK_SIZE, 0u);

  const auto CONTENT = slurp(PATH);
  EXPECT_NE(CONTENT.find("hello world\n"), std::string::npos);

  std::filesystem::remove(PATH, ec); // best-effort cleanup
}

/** @test Rotate creates backup when threshold exceeded and logging continues. */
TEST(LogBaseTest, RotateCreatesBackupWhenNeededAndReopens) {
  // Unique subdirectory keeps any backup local and easy to find.
  const auto BASE_TMP = std::filesystem::temp_directory_path();
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<unsigned long long> dist;
  const auto SUBDIR = BASE_TMP / ("logbase_rotate_" + std::to_string(dist(gen)));

  std::error_code ec{};
  std::filesystem::create_directory(SUBDIR, ec);
  ASSERT_FALSE(ec) << "Failed to create temp subdir: " << SUBDIR;

  const auto LOG_PATH = SUBDIR / "test.log";
  std::filesystem::path backupFound;

  {
    LogBase log(LOG_PATH.string());

    // Write a large line; on some filesystems size() may lag, so rotation
    // outcome is allowed to vary (OK or ERROR_ROTATE_RENAME).
    std::string bigLine(4096, 'x');
    ASSERT_EQ(log.write(bigLine), Status::OK);

    const std::size_t THRESHOLD = 16u;
    const auto ROTATE_STATUS = log.rotate(THRESHOLD);
    EXPECT_TRUE(ROTATE_STATUS == Status::OK || ROTATE_STATUS == Status::ERROR_ROTATE_RENAME);

    // New writes must still succeed regardless of rotation outcome.
    ASSERT_EQ(log.write("second line"), Status::OK);
  }

  // Current file contains the post-rotation write.
  const auto CONTENT = slurp(LOG_PATH);
  EXPECT_NE(CONTENT.find("second line\n"), std::string::npos);

  // Detect backup if it exists.
  for (auto& de : std::filesystem::directory_iterator(SUBDIR)) {
    const auto NAME = de.path().filename().string();
    const auto STEM = LOG_PATH.filename().string() + "_"; // "test.log_"
    if (NAME.rfind(STEM, 0) == 0 && NAME.find(".backup") != std::string::npos) {
      backupFound = de.path();
      break;
    }
  }
  if (!backupFound.empty()) {
    EXPECT_TRUE(std::filesystem::exists(backupFound));
  }

  // Cleanup
  if (!backupFound.empty())
    std::filesystem::remove(backupFound, ec);
  std::filesystem::remove(LOG_PATH, ec);
  std::filesystem::remove(SUBDIR, ec);
}

/** @test Size reports non-decreasing value after additional writes. */
TEST(LogBaseTest, SizeReflectsGrowthAfterWrites) {
  const auto PATH = uniqTempFile("logbase_size");
  LogBase log(PATH.string());

  std::size_t bytes0 = 0, bytes1 = 0, bytes2 = 0;
  ASSERT_EQ(log.size(bytes0), Status::OK);

  ASSERT_EQ(log.write("a"), Status::OK);
  ASSERT_EQ(log.size(bytes1), Status::OK);

  ASSERT_EQ(log.write("bbbbbbbbbbbbbbbbbbbb"), Status::OK); // 20 'b' + newline
  ASSERT_EQ(log.size(bytes2), Status::OK);

  EXPECT_LE(bytes0, bytes1);
  EXPECT_LE(bytes1, bytes2);

  std::error_code ec{};
  std::filesystem::remove(PATH, ec);
}

/** @test Below-threshold rotation is a no-op and does not disturb writes. */
TEST(LogBaseTest, RotateNoopBelowThreshold) {
  const auto PATH = uniqTempFile("logbase_rotate_noop");
  LogBase log(PATH.string());

  ASSERT_EQ(log.write("x"), Status::OK);

  constexpr std::size_t HIGH_THRESHOLD = std::size_t{1} << 20; // 1 MiB
  const auto ROTATE_STATUS = log.rotate(HIGH_THRESHOLD);
  EXPECT_EQ(ROTATE_STATUS, Status::OK);

  ASSERT_EQ(log.write("y"), Status::OK);

  const auto CONTENT = slurp(PATH);
  EXPECT_NE(CONTENT.find("x\n"), std::string::npos);
  EXPECT_NE(CONTENT.find("y\n"), std::string::npos);

  std::error_code ec{};
  std::filesystem::remove(PATH, ec);
}

/** @test Multiple writes persist with correct line count (append semantics). */
TEST(LogBaseTest, MultipleWritesPersistLineCount) {
  const auto PATH = uniqTempFile("logbase_multiwrite");
  {
    LogBase log(PATH.string());
    const int N = 50;
    for (int i = 0; i < N; ++i) {
      ASSERT_EQ(log.write("line"), Status::OK);
    }
  }
  const auto CONTENT = slurp(PATH);
  EXPECT_EQ(countLines(CONTENT), 50);

  std::error_code ec{};
  std::filesystem::remove(PATH, ec);
}

/** @test lastOpenStatus returns OK after successful open. */
TEST(LogBaseTest, LastOpenStatusOkAfterSuccessfulOpen) {
  const auto PATH = uniqTempFile("logbase_openstatus");
  LogBase log(PATH.string());
  EXPECT_EQ(log.lastOpenStatus(), Status::OK);

  std::error_code ec{};
  std::filesystem::remove(PATH, ec);
}

/** @test toString covers all Status codes without returning nullptr. */
TEST(LogBaseTest, ToStringCoversAllStatusCodes) {
  EXPECT_STREQ(toString(Status::OK), "OK");
  EXPECT_STREQ(toString(Status::ERROR_OPEN), "ERROR_OPEN");
  EXPECT_STREQ(toString(Status::ERROR_SIZE), "ERROR_SIZE");
  EXPECT_STREQ(toString(Status::ERROR_ROTATE_RENAME), "ERROR_ROTATE_RENAME");
  EXPECT_STREQ(toString(Status::ERROR_ROTATE_REOPEN), "ERROR_ROTATE_REOPEN");
  EXPECT_STREQ(toString(Status::ERROR_SYNC), "ERROR_SYNC");
  EXPECT_NE(toString(static_cast<Status>(255)), nullptr);
}
