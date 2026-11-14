/**
 * @file SystemLog_uTest.cpp
 * @brief Unit tests for SystemLog categorized logging.
 */

#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>
#include <algorithm>

using logs::Status;
using logs::SystemLog;

/* ----------------------------- Test Helpers ----------------------------- */

static std::filesystem::path uniqTempFile(const std::string& stem) {
  const auto DIR = std::filesystem::temp_directory_path();
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<unsigned long long> dist;
  return DIR / (stem + "_" + std::to_string(dist(gen)) + ".log");
}

static std::string slurp(const std::filesystem::path& p) {
  std::ifstream ifs(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

/// Newlines in a file, read in blocks so a large file stays cheap to count.
static long countNewlines(const std::filesystem::path& p) {
  std::ifstream ifs(p, std::ios::binary);
  std::vector<char> block(64 * 1024);
  long lines = 0;
  while (ifs) {
    ifs.read(block.data(), static_cast<std::streamsize>(block.size()));
    lines += std::count(block.data(), block.data() + ifs.gcount(), '\n');
  }
  return lines;
}

static bool contains(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}

/* ----------------------------- API Tests ----------------------------- */

/** @test INFO line contains stable parts (level, source, message). */
TEST(SystemLogTest, InfoLineFormat) {
  const auto PATH = uniqTempFile("systemlog_info");
  {
    SystemLog log(PATH.string());
    ASSERT_EQ(log.info("EXECUTIVE", "hello world", /*toConsole*/ false), Status::OK);
  }
  const auto S = slurp(PATH);
  EXPECT_TRUE(contains(S, " INFO: EXECUTIVE - hello world\n"));

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

/** @test WARNING line includes error code in brackets. */
TEST(SystemLogTest, WarningIncludesErrorCode) {
  const auto PATH = uniqTempFile("systemlog_warn");
  {
    SystemLog log(PATH.string());
    ASSERT_EQ(log.warning("SCHEDULER_BASE", /*ec*/ 8, "wheel slip detected", false), Status::OK);
  }
  const auto S = slurp(PATH);
  EXPECT_TRUE(contains(S, " WARNING: SCHEDULER_BASE[8] - wheel slip detected\n"));

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

/** @test Multiple writes append; lines are not overwritten. */
TEST(SystemLogTest, AppendsMultipleLines) {
  const auto PATH = uniqTempFile("systemlog_append");
  {
    SystemLog log(PATH.string());
    ASSERT_EQ(log.info("A", "one", false), Status::OK);
    ASSERT_EQ(log.error("B", 1, "two", false), Status::OK);
    ASSERT_EQ(log.warning("C", 7, "three", false), Status::OK);
  }
  const auto S = slurp(PATH);
  EXPECT_TRUE(contains(S, " INFO: A - one\n"));
  EXPECT_TRUE(contains(S, " ERROR: B[1] - two\n"));
  EXPECT_TRUE(contains(S, " WARNING: C[7] - three\n"));

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

/** @test Threshold filtering: below-level messages are skipped. */
TEST(SystemLogTest, ThresholdFilteringSkipsBelowLevel) {
  const auto PATH = uniqTempFile("systemlog_threshold");
  {
    SystemLog log(PATH.string());
    log.setLevel(SystemLog::Level::ERROR); // allow ERROR and above only
    ASSERT_EQ(log.debug("DBG", "hidden", false), Status::OK);
    ASSERT_EQ(log.info("INF", "hidden", false), Status::OK);
    ASSERT_EQ(log.warning("WRN", 3, "hidden", false), Status::OK);
    ASSERT_EQ(log.error("ERR", 1, "visible", false), Status::OK);
  }
  const auto S = slurp(PATH);
  EXPECT_FALSE(contains(S, " DEBUG: DBG - hidden\n"));
  EXPECT_FALSE(contains(S, " INFO: INF - hidden\n"));
  EXPECT_FALSE(contains(S, " WARNING: WRN[3] - hidden\n"));
  EXPECT_TRUE(contains(S, " ERROR: ERR[1] - visible\n"));

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

/** @test Non-blocking mode: writes still succeed functionally. */
TEST(SystemLogTest, NonBlockingModeWrites) {
  const auto PATH = uniqTempFile("systemlog_nonblocking");
  {
    SystemLog log(PATH.string());
    log.setNonBlocking(true);
    ASSERT_EQ(log.info("NB", "ok", false), Status::OK);
  }
  const auto S = slurp(PATH);
  EXPECT_TRUE(contains(S, " INFO: NB - ok\n"));

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

/** @test Console echo: call should succeed; we do not capture stdout here. */
TEST(SystemLogTest, ConsoleEchoDoesNotThrow) {
  const auto PATH = uniqTempFile("systemlog_console");
  {
    SystemLog log(PATH.string());
    // Expect OK even if console is not writable; implementation swallows failures.
    ASSERT_EQ(log.info("CON", "visible in file; echoed to console", /*toConsole*/ true),
              Status::OK);
  }
  const auto S = slurp(PATH);
  EXPECT_TRUE(contains(S, " INFO: CON - visible in file; echoed to console\n"));

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

/** @test Verbosity defaults to 0; setVerbosity/verbosity getter and setter round-trip. */
TEST(SystemLogTest, VerbosityGetterSetterRoundTrip) {
  const auto PATH = uniqTempFile("systemlog_verbosity");
  SystemLog log(PATH.string());
  EXPECT_EQ(log.verbosity(), 0u);
  log.setVerbosity(5);
  EXPECT_EQ(log.verbosity(), 5u);
  log.setVerbosity(0);
  EXPECT_EQ(log.verbosity(), 0u);

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

/** @test Default mode is SYNC; mode() and isAsync() reflect initial state. */
TEST(SystemLogTest, DefaultModeIsSyncAndIsAsyncFalse) {
  const auto PATH = uniqTempFile("systemlog_mode");
  SystemLog log(PATH.string());
  EXPECT_EQ(log.mode(), SystemLog::Mode::SYNC);
  EXPECT_FALSE(log.isAsync());

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

/** @test fatal() writes a FATAL line to the log file. */
TEST(SystemLogTest, FatalLineFormat) {
  const auto PATH = uniqTempFile("systemlog_fatal");
  {
    SystemLog log(PATH.string());
    ASSERT_EQ(log.fatal("EXEC", /*ec*/ 42, "system halt", false), Status::OK);
  }
  const auto S = slurp(PATH);
  EXPECT_TRUE(contains(S, " FATAL: EXEC[42] - system halt\n"));

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

/* ----------------------------- Rotation Under Load ----------------------------- */

/** @test Preserves every accepted line across concurrent writes and rotations */
TEST(SystemLogTest, RotateWhileLogging) {
  // A directory of its own keeps the live log and its backups together.
  const auto DIR = uniqTempFile("systemlog_rotate_race").replace_extension();
  std::error_code ec;
  std::filesystem::create_directory(DIR, ec);
  ASSERT_FALSE(ec) << "Failed to create temp dir: " << DIR;
  const auto PATH = DIR / "rotate.log";

  constexpr std::size_t LIMIT = 64;
  constexpr int ROTATIONS = 20;
  constexpr auto BUDGET = std::chrono::seconds(10);

  int rotations = 0;
  int rotateFailures = 0;
  std::atomic<long> writesOk{0};
  std::atomic<int> writeFailures{0};

  {
    logs::SystemLog log(PATH.string(), logs::SystemLog::Mode::SYNC);
    log.setLevel(logs::SystemLog::Level::INFO);
    ASSERT_EQ(log.lastOpenStatus(), logs::Status::OK);

    std::atomic<bool> stop{false};
    std::thread writer([&] {
      while (!stop.load(std::memory_order_relaxed)) {
        if (log.info("ROT", "rotation load line", false) == logs::Status::OK) {
          writesOk.fetch_add(1, std::memory_order_relaxed);
        } else {
          writeFailures.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });

    // Rotate only once the writer has pushed the live file past the limit,
    // so each rotation is a real rename+reopen under live traffic (the
    // workload that shows the openStatus_ read/write pair to race
    // detectors). The writer has to make progress before the deadline; the
    // deadline bounds this polling only, not the join and the file checks
    // that follow.
    const auto DEADLINE = std::chrono::steady_clock::now() + BUDGET;
    while (rotations < ROTATIONS && std::chrono::steady_clock::now() < DEADLINE) {
      std::size_t bytes = 0;
      (void)log.size(bytes);
      if (bytes <= LIMIT) {
        std::this_thread::sleep_for(std::chrono::microseconds(200));
        continue;
      }
      if (log.rotate(LIMIT) != logs::Status::OK) {
        ++rotateFailures;
        break;
      }
      ++rotations;
    }

    stop.store(true, std::memory_order_relaxed);
    writer.join();

    EXPECT_EQ(rotations, ROTATIONS) << "the live file never refilled within the budget";
    EXPECT_EQ(rotateFailures, 0);
    EXPECT_EQ(writeFailures.load(), 0);
    EXPECT_EQ(log.lastOpenStatus(), logs::Status::OK);
  }

  // Every rotation left its own backup, and no line was lost between files.
  int backups = 0;
  long linesOnDisk = 0;
  for (const auto& entry : std::filesystem::directory_iterator(DIR)) {
    linesOnDisk += countNewlines(entry.path());
    if (entry.path().extension() == ".backup") {
      ++backups;
    }
  }
  EXPECT_EQ(backups, rotations);
  EXPECT_EQ(linesOnDisk, writesOk.load());

  std::filesystem::remove_all(DIR, ec);
}

/** @test With the fatal-flush hook enabled, fatal() returns with the queue drained. */
TEST(SystemLogTest, FatalFlushDrainsQueue) {
  const auto PATH = std::filesystem::temp_directory_path() / "logs_fatal_flush_test.log";
  std::filesystem::remove(PATH);

  {
    logs::SystemLog log(PATH.string(), logs::SystemLog::Mode::ASYNC);
    log.setLevel(logs::SystemLog::Level::INFO);
    ASSERT_TRUE(log.isAsync());
    log.setFatalFlush(true);

    for (int i = 0; i < 64; ++i) {
      ASSERT_EQ(log.info("FF", "pre-fatal entry", false), logs::Status::OK);
    }
    ASSERT_EQ(log.fatal("FF", 42, "fatal with flush", false), logs::Status::OK);
    EXPECT_EQ(log.asyncBackend()->queueDepth(), 0u);
  }

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}
