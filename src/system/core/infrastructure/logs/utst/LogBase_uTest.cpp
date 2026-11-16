/**
 * @file LogBase_uTest.cpp
 * @brief Unit tests for LogBase I/O, queries, and rotation behavior.
 */

#include "src/system/core/infrastructure/logs/inc/LogBase.hpp"
#include "src/system/core/infrastructure/logs/utst/WallClockWindow.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

using logs::LogBase;
using logs::Status;
using logs::test::WALL_CLOCK_ATTEMPTS;
using logs::test::WallClockWindow;

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

/// Open descriptors of this process (the entries of /proc/self/fd).
static int openDescriptorCount() {
  int count = 0;
  for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) {
    ++count;
  }
  return count;
}

/// First-choice backup name of a rotation at time T: `<log>_YYYYMMDD-HHMMSS.backup`.
static std::filesystem::path backupNameAt(const std::filesystem::path& log, std::time_t t) {
  std::tm tmLocal{};
  localtime_r(&t, &tmLocal);
  std::ostringstream oss;
  oss << std::put_time(&tmLocal, "%Y%m%d-%H%M%S");
  return std::filesystem::path(log.string() + "_" + oss.str() + ".backup");
}

/// Rotate twice while every first-choice backup name of a time window is
/// already taken, by regular files or by dangling symlinks. Each rotation
/// must leave the taken entries as they were and put its own lines in a
/// suffixed backup beside one of them. A rotation outside the window finds
/// no taken name and fails the suffix count, so slow rotations cannot pass
/// without meeting a collision. JUDGED is set once the checks have run; it
/// stays false when the wall clock was set between taking the names and
/// rotating, which puts the rotations outside the window for no fault of
/// theirs.
static void rotateOntoTakenNames(bool symlinks, bool& judged) {
  const auto BASE_TMP = std::filesystem::temp_directory_path();
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<unsigned long long> dist;
  const auto SUBDIR = BASE_TMP / ("logbase_rotate_taken_" + std::to_string(dist(gen)));

  std::error_code ec{};
  std::filesystem::create_directory(SUBDIR, ec);
  ASSERT_FALSE(ec) << "Failed to create temp subdir: " << SUBDIR;

  const auto LOG_PATH = SUBDIR / "test.log";
  constexpr int WINDOW_S = 60;
  constexpr int ROTATIONS = 2;

  std::set<std::filesystem::path> taken;
  const WallClockWindow WINDOW;
  const std::time_t NOW = std::time(nullptr);
  for (int dt = -1; dt <= WINDOW_S; ++dt) {
    const auto NAME = backupNameAt(LOG_PATH, NOW + dt);
    if (!taken.insert(NAME).second) {
      continue; // a repeated local second maps to a name already taken
    }
    if (symlinks) {
      std::filesystem::create_symlink(SUBDIR / "no_such_target", NAME, ec);
      ASSERT_FALSE(ec) << "Failed to create symlink: " << NAME;
    } else {
      std::ofstream(NAME) << "occupied\n";
    }
  }

  {
    LogBase log(LOG_PATH.string());
    for (int i = 0; i < ROTATIONS; ++i) {
      ASSERT_EQ(log.write("rotated line " + std::to_string(i)), Status::OK);
      ASSERT_EQ(log.rotate(1u), Status::OK);
    }
  }
  if (WINDOW.close().empty()) {
    std::filesystem::remove_all(SUBDIR, ec);
    return;
  }
  judged = true;

  // The taken entries are what they were.
  for (const auto& NAME : taken) {
    if (symlinks) {
      EXPECT_TRUE(std::filesystem::is_symlink(std::filesystem::symlink_status(NAME))) << NAME;
      EXPECT_EQ(std::filesystem::read_symlink(NAME, ec), SUBDIR / "no_such_target") << NAME;
    } else {
      EXPECT_EQ(slurp(NAME), "occupied\n") << NAME;
    }
  }

  // Each rotation produced `<taken stem>-N.backup` beside a taken name.
  int suffixed = 0;
  std::string rotated;
  for (auto& de : std::filesystem::directory_iterator(SUBDIR)) {
    const auto NAME = de.path().filename().string();
    const auto DASH = NAME.rfind('-');
    if (taken.count(de.path()) != 0 || DASH == std::string::npos || NAME.size() < 7 ||
        NAME.compare(NAME.size() - 7, 7, ".backup") != 0) {
      continue;
    }
    const auto FIRST_CHOICE = SUBDIR / (NAME.substr(0, DASH) + ".backup");
    if (taken.count(FIRST_CHOICE) != 0) {
      ++suffixed;
      rotated += slurp(de.path());
    }
  }
  EXPECT_EQ(suffixed, ROTATIONS) << "a rotation met no taken name or replaced one";
  EXPECT_EQ(countLines(rotated), ROTATIONS);
  for (int i = 0; i < ROTATIONS; ++i) {
    EXPECT_NE(rotated.find("rotated line " + std::to_string(i) + "\n"), std::string::npos);
  }

  std::filesystem::remove_all(SUBDIR, ec);
}

/// Runs rotateOntoTakenNames until its checks have run.
static void expectRotationKeepsTakenNames(bool symlinks) {
  bool judged = false;
  for (int attempt = 0;
       attempt < WALL_CLOCK_ATTEMPTS && !judged && !testing::Test::HasFatalFailure(); ++attempt) {
    rotateOntoTakenNames(symlinks, judged);
  }
  if (!testing::Test::HasFatalFailure()) {
    EXPECT_TRUE(judged) << "the wall clock was set, or the rotations outlasted the window, "
                        << "in each of " << WALL_CLOCK_ATTEMPTS << " attempts";
  }
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

/** @test Moves the content to one timestamped backup and restarts the live file */
TEST(LogBaseTest, RotateCreatesBackupWhenNeededAndReopens) {
  const auto BASE_TMP = std::filesystem::temp_directory_path();
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<unsigned long long> dist;
  const std::string BIG_LINE(4096, 'x');

  // The rotation runs inside a window of wall-clock seconds. A window the
  // clock was set in says nothing about the backup's name, so the rotation
  // is repeated in a fresh directory.
  std::error_code ec{};
  std::filesystem::path subdir;
  std::filesystem::path logPath;
  std::vector<std::time_t> seconds;
  for (int attempt = 0; attempt < WALL_CLOCK_ATTEMPTS && seconds.empty(); ++attempt) {
    std::filesystem::remove_all(subdir, ec);

    // A directory of its own, so the entries a rotation leaves can be counted.
    subdir = BASE_TMP / ("logbase_rotate_" + std::to_string(dist(gen)));
    std::filesystem::create_directory(subdir, ec);
    ASSERT_FALSE(ec) << "Failed to create temp subdir: " << subdir;
    logPath = subdir / "test.log";

    LogBase log(logPath.string());
    ASSERT_EQ(log.write(BIG_LINE), Status::OK);

    const WallClockWindow WINDOW;
    ASSERT_EQ(log.rotate(16u), Status::OK);
    seconds = WINDOW.close();
    EXPECT_EQ(log.lastOpenStatus(), Status::OK);

    ASSERT_EQ(log.write("second line"), Status::OK);
  }
  ASSERT_FALSE(seconds.empty()) << "the wall clock was set, or the rotation outlasted the window, "
                                << "in each of " << WALL_CLOCK_ATTEMPTS << " attempts";

  // The live file restarted: it holds the write made after the rotation only.
  EXPECT_EQ(slurp(logPath), "second line\n");

  // The directory holds the live file and one backup, named for one of the
  // window's seconds, with the content written before the rotation.
  std::set<std::filesystem::path> expectedNames;
  for (const std::time_t T : seconds) {
    expectedNames.insert(backupNameAt(logPath, T));
  }
  std::vector<std::filesystem::path> others;
  for (const auto& de : std::filesystem::directory_iterator(subdir)) {
    if (de.path() != logPath) {
      others.push_back(de.path());
    }
  }
  ASSERT_EQ(others.size(), 1u);
  EXPECT_EQ(expectedNames.count(others.front()), 1u) << others.front();
  EXPECT_EQ(slurp(others.front()), BIG_LINE + "\n");

  std::filesystem::remove_all(subdir, ec);
}

/** @test Preserves an existing backup when a rotation's timestamped name is taken */
TEST(LogBaseTest, RotateOntoTakenBackupNameKeepsBoth) { expectRotationKeepsTakenNames(false); }

/** @test Preserves a dangling symlink that holds a rotation's timestamped name */
TEST(LogBaseTest, RotateOntoDanglingSymlinkNameKeepsIt) { expectRotationKeepsTakenNames(true); }

/** @test Keeps the log descriptor and status when the backup rename fails */
TEST(LogBaseTest, RotateRenameFailureKeepsDescriptor) {
  if (::geteuid() == 0) {
    GTEST_SKIP() << "root is not refused by directory permissions";
  }

  const auto BASE_TMP = std::filesystem::temp_directory_path();
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<unsigned long long> dist;
  const auto SUBDIR = BASE_TMP / ("logbase_rotate_denied_" + std::to_string(dist(gen)));

  std::error_code ec{};
  std::filesystem::create_directory(SUBDIR, ec);
  ASSERT_FALSE(ec) << "Failed to create temp subdir: " << SUBDIR;
  const auto LOG_PATH = SUBDIR / "test.log";

  {
    LogBase log(LOG_PATH.string());
    ASSERT_EQ(log.write("before the refused rotations"), Status::OK);

    // A directory without write permission refuses the rename; the open
    // log file itself stays writable through its descriptor.
    std::filesystem::permissions(
        SUBDIR, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec, ec);
    EXPECT_FALSE(ec);

    const int OPEN_BEFORE = openDescriptorCount();
    for (int i = 0; i < 3; ++i) {
      EXPECT_EQ(log.rotate(1u), Status::ERROR_ROTATE_RENAME);
    }
    EXPECT_EQ(openDescriptorCount(), OPEN_BEFORE);
    EXPECT_EQ(log.lastOpenStatus(), Status::OK);
    EXPECT_EQ(log.write("after the refused rotations"), Status::OK);

    std::filesystem::permissions(SUBDIR, std::filesystem::perms::owner_all, ec);
  }

  const auto CONTENT = slurp(LOG_PATH);
  EXPECT_NE(CONTENT.find("before the refused rotations\n"), std::string::npos);
  EXPECT_NE(CONTENT.find("after the refused rotations\n"), std::string::npos);

  std::filesystem::remove_all(SUBDIR, ec);
}

/** @test Reports ERROR_SIZE from rotate and size when the log's size cannot be read */
TEST(LogBaseTest, RotateAndSizeReportSizeFailure) {
  const auto PATH = uniqTempFile("logbase_size_failure");
  LogBase log(PATH.string());
  ASSERT_EQ(log.write("line"), Status::OK);

  std::error_code ec{};
  std::filesystem::remove(PATH, ec);
  ASSERT_FALSE(ec);

  std::size_t bytes = 0;
  EXPECT_EQ(log.size(bytes), Status::ERROR_SIZE);
  EXPECT_EQ(log.rotate(0u), Status::ERROR_SIZE);
}

/** @test Reports ERROR_OPEN from every call when the log cannot be opened */
TEST(LogBaseTest, UnopenablePathReportsOpenError) {
  const auto PATH = uniqTempFile("logbase_no_such_dir").replace_extension() / "x.log";
  LogBase log(PATH.string());

  EXPECT_EQ(log.lastOpenStatus(), Status::ERROR_OPEN);
  EXPECT_EQ(log.write("nowhere to go"), Status::ERROR_OPEN);
  EXPECT_EQ(log.flush(), Status::ERROR_OPEN);
  EXPECT_EQ(log.writeErrorCount(), 0u);
}

/** @test Reports ERROR_WRITE and counts it when the device refuses the write */
TEST(LogBaseTest, WriteFailureIsReportedAndCounted) {
  // A device that accepts the open and refuses every write: no space left.
  LogBase log("/dev/full");
  ASSERT_EQ(log.lastOpenStatus(), Status::OK);

  EXPECT_EQ(log.writeErrorCount(), 0u);
  EXPECT_EQ(log.write("does not fit"), Status::ERROR_WRITE);
  EXPECT_EQ(log.write("does not fit either"), Status::ERROR_WRITE);
  EXPECT_EQ(log.writeErrorCount(), 2u);
}

/** @test Size reports the bytes each write appends: the message and its newline */
TEST(LogBaseTest, SizeReflectsGrowthAfterWrites) {
  const auto PATH = uniqTempFile("logbase_size");
  LogBase log(PATH.string());

  // Start from a value no size in this test can be, so an untouched output
  // parameter cannot pass for a size.
  constexpr std::size_t UNSET = 999;
  std::size_t bytes0 = UNSET, bytes1 = UNSET, bytes2 = UNSET;
  ASSERT_EQ(log.size(bytes0), Status::OK);

  ASSERT_EQ(log.write("a"), Status::OK);
  ASSERT_EQ(log.size(bytes1), Status::OK);

  ASSERT_EQ(log.write("bbbbbbbbbbbbbbbbbbbb"), Status::OK); // 20 'b' + newline
  ASSERT_EQ(log.size(bytes2), Status::OK);

  EXPECT_EQ(bytes0, 0u);
  EXPECT_EQ(bytes1, 2u);
  EXPECT_EQ(bytes2, 23u);

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

/** @test toString names every Status code and reports any other value as UNKNOWN */
TEST(LogBaseTest, ToStringCoversAllStatusCodes) {
  EXPECT_STREQ(toString(Status::OK), "OK");
  EXPECT_STREQ(toString(Status::ERROR_OPEN), "ERROR_OPEN");
  EXPECT_STREQ(toString(Status::ERROR_SIZE), "ERROR_SIZE");
  EXPECT_STREQ(toString(Status::ERROR_ROTATE_RENAME), "ERROR_ROTATE_RENAME");
  EXPECT_STREQ(toString(Status::ERROR_ROTATE_REOPEN), "ERROR_ROTATE_REOPEN");
  EXPECT_STREQ(toString(Status::ERROR_SYNC), "ERROR_SYNC");
  EXPECT_STREQ(toString(Status::ERROR_WRITE), "ERROR_WRITE");

  // ERROR_WRITE is the last enumerator: the next value and the largest one
  // are outside the enum.
  const auto PAST_LAST =
      static_cast<std::uint8_t>(static_cast<std::uint8_t>(Status::ERROR_WRITE) + 1);
  EXPECT_STREQ(toString(static_cast<Status>(PAST_LAST)), "UNKNOWN");
  EXPECT_STREQ(toString(static_cast<Status>(255)), "UNKNOWN");
}
