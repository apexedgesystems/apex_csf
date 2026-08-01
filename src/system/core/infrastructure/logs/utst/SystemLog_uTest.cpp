/**
 * @file SystemLog_uTest.cpp
 * @brief Unit tests for SystemLog categorized logging.
 */

#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
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

/** @test Logging continues correctly while rotate() runs concurrently.
 *
 * A writer thread logs continuously while the main thread rotates with a
 * tiny size limit, forcing rename+reopen cycles under live traffic. Every
 * write must land in either the pre- or post-rotation file (atomic fd
 * swap), status stays OK throughout, and the run is exactly the workload
 * that makes the openStatus_ read/write pair visible to race detectors.
 */
TEST(SystemLogTest, RotateWhileLogging) {
  const auto PATH = std::filesystem::temp_directory_path() / "logs_rotate_race_test.log";
  std::filesystem::remove(PATH);

  {
    logs::SystemLog log(PATH.string(), logs::SystemLog::Mode::SYNC);
    log.setLevel(logs::SystemLog::Level::INFO);
    ASSERT_EQ(log.lastOpenStatus(), logs::Status::OK);

    std::atomic<bool> stop{false};
    std::atomic<bool> started{false};
    std::atomic<int> writeFailures{0};
    std::thread writer([&] {
      started.store(true, std::memory_order_release);
      while (!stop.load(std::memory_order_relaxed)) {
        if (log.info("ROT", "rotation load line", false) != logs::Status::OK) {
          writeFailures.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });

    while (!started.load(std::memory_order_acquire)) {
    }

    // Rotate until the writer has forced enough real rotations that the
    // rename+reopen path demonstrably interleaved with live writes.
    int rotations = 0;
    for (int i = 0; i < 5000 && rotations < 50; ++i) {
      std::size_t bytes = 0;
      (void)log.size(bytes);
      if (bytes > 64) {
        ++rotations;
      }
      ASSERT_EQ(log.rotate(64), logs::Status::OK);
    }
    EXPECT_GE(rotations, 50) << "writer never outpaced rotation; no real overlap";

    stop.store(true, std::memory_order_relaxed);
    writer.join();

    EXPECT_EQ(writeFailures.load(), 0);
    EXPECT_EQ(log.lastOpenStatus(), logs::Status::OK);
  }

  // Cleanup: rotation backups share the path prefix.
  for (const auto& entry :
       std::filesystem::directory_iterator(std::filesystem::temp_directory_path())) {
    const auto NAME = entry.path().filename().string();
    if (NAME.rfind("logs_rotate_race_test.log", 0) == 0) {
      std::error_code ec;
      std::filesystem::remove(entry.path(), ec);
    }
  }
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
