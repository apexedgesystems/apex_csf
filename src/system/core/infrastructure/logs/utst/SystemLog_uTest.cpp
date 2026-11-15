/**
 * @file SystemLog_uTest.cpp
 * @brief Unit tests for SystemLog categorized logging.
 */

#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

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
  constexpr std::size_t BLOCK_BYTES = std::size_t{64} * 1024;
  std::vector<char> block(BLOCK_BYTES);
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

/// The lines of a file's content, without their newlines.
static std::vector<std::string> splitLines(const std::string& content) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  for (std::size_t nl = content.find('\n'); nl != std::string::npos;
       nl = content.find('\n', start)) {
    lines.push_back(content.substr(start, nl - start));
    start = nl + 1;
  }
  return lines;
}

/// Length of a line's "[YYYY-MM-DD HH:MM:SS]" prefix.
constexpr std::size_t STAMP_LEN = 21;

/// The prefix of a line written during local second T.
static std::string stampAt(std::time_t t) {
  std::tm tmLocal{};
  localtime_r(&t, &tmLocal);
  std::array<char, 32> buf{};
  (void)std::strftime(buf.data(), buf.size(), "[%Y-%m-%d %H:%M:%S]", &tmLocal);
  return std::string(buf.data());
}

/* ----------------------------- API Tests ----------------------------- */

/** @test Writes each level's whole line: the time of the call, level, source, code and message */
TEST(SystemLogTest, LineFormatPerLevel) {
  const std::vector<std::string> TAILS = {
      " DEBUG: DBG - detail",
      " INFO: EXECUTIVE - hello world",
      " WARNING: SCHEDULER_BASE[8] - wheel slip detected",
      " ERROR: IO[255] - bus fault",
      " FATAL: EXEC[42] - system halt",
  };

  for (const auto MODE : {SystemLog::Mode::SYNC, SystemLog::Mode::ASYNC}) {
    SCOPED_TRACE(MODE == SystemLog::Mode::ASYNC ? "ASYNC" : "SYNC");
    const auto PATH = uniqTempFile("systemlog_format");
    std::string content;
    const std::time_t BEFORE = std::time(nullptr);
    {
      SystemLog log(PATH.string(), MODE);
      log.setLevel(SystemLog::Level::DEBUG);
      ASSERT_EQ(log.debug("DBG", "detail", 0), Status::OK);
      ASSERT_EQ(log.info("EXECUTIVE", "hello world", false), Status::OK);
      ASSERT_EQ(log.warning("SCHEDULER_BASE", /*ec*/ 8, "wheel slip detected", false), Status::OK);
      ASSERT_EQ(log.error("IO", /*ec*/ 255, "bus fault", false), Status::OK);
      ASSERT_EQ(log.fatal("EXEC", /*ec*/ 42, "system halt", false), Status::OK);
      ASSERT_EQ(log.flush(), Status::OK);
      content = slurp(PATH);
    }
    const std::time_t AFTER = std::time(nullptr);

    // Each line starts with the timestamp of one of the seconds between the
    // first call and the read, and the level follows it directly.
    std::set<std::string> stamps;
    for (std::time_t t = BEFORE; t <= AFTER; ++t) {
      stamps.insert(stampAt(t));
    }
    const auto LINES = splitLines(content);
    ASSERT_EQ(LINES.size(), TAILS.size());
    for (std::size_t i = 0; i < LINES.size(); ++i) {
      SCOPED_TRACE(LINES[i]);
      ASSERT_GE(LINES[i].size(), STAMP_LEN);
      EXPECT_EQ(stamps.count(LINES[i].substr(0, STAMP_LEN)), 1u)
          << "the line does not start with [YYYY-MM-DD HH:MM:SS] of the call";
      EXPECT_EQ(LINES[i].substr(STAMP_LEN), TAILS[i]);
    }

    std::error_code ec;
    std::filesystem::remove(PATH, ec);
  }
}

/** @test Appends each line in order in SYNC and in ASYNC mode */
TEST(SystemLogTest, AppendsMultipleLines) {
  for (const auto MODE : {SystemLog::Mode::SYNC, SystemLog::Mode::ASYNC}) {
    SCOPED_TRACE(MODE == SystemLog::Mode::ASYNC ? "ASYNC" : "SYNC");
    const auto PATH = uniqTempFile("systemlog_append");
    std::string content;
    {
      SystemLog log(PATH.string(), MODE);
      ASSERT_EQ(log.isAsync(), MODE == SystemLog::Mode::ASYNC);
      ASSERT_EQ(log.info("A", "one", false), Status::OK);
      ASSERT_EQ(log.error("B", 1, "two", false), Status::OK);
      ASSERT_EQ(log.warning("C", 7, "three", false), Status::OK);
      // Read while the log is alive: only flush() can have put the lines there.
      ASSERT_EQ(log.flush(), Status::OK);
      content = slurp(PATH);
    }
    const auto ONE = content.find(" INFO: A - one\n");
    const auto TWO = content.find(" ERROR: B[1] - two\n");
    const auto THREE = content.find(" WARNING: C[7] - three\n");
    ASSERT_NE(ONE, std::string::npos);
    ASSERT_NE(TWO, std::string::npos);
    ASSERT_NE(THREE, std::string::npos);
    EXPECT_LT(ONE, TWO);
    EXPECT_LT(TWO, THREE);
    EXPECT_EQ(std::count(content.begin(), content.end(), '\n'), 3);

    std::error_code ec;
    std::filesystem::remove(PATH, ec);
  }
}

/** @test Threshold filtering: below-level messages are skipped. */
TEST(SystemLogTest, ThresholdFilteringSkipsBelowLevel) {
  const auto PATH = uniqTempFile("systemlog_threshold");
  {
    SystemLog log(PATH.string());
    EXPECT_EQ(log.level(), SystemLog::Level::INFO); // the default threshold
    log.setLevel(SystemLog::Level::ERROR);          // allow ERROR and above only
    EXPECT_EQ(log.level(), SystemLog::Level::ERROR);
    ASSERT_EQ(log.debug("DBG", "hidden", 0), Status::OK);
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

/** @test Echoes a line to stdout only when asked, byte for byte as the file holds it */
TEST(SystemLogTest, ConsoleEchoWritesTheLineToStdout) {
  for (const auto MODE : {SystemLog::Mode::SYNC, SystemLog::Mode::ASYNC}) {
    SCOPED_TRACE(MODE == SystemLog::Mode::ASYNC ? "ASYNC" : "SYNC");
    const auto PATH = uniqTempFile("systemlog_console");
    Status quiet = Status::ERROR_OPEN;
    Status echoed = Status::ERROR_OPEN;
    Status flushed = Status::ERROR_OPEN;
    std::string content;

    // No assertion runs while stdout is captured, so the capture always ends.
    testing::internal::CaptureStdout();
    {
      SystemLog log(PATH.string(), MODE);
      quiet = log.info("CON", "file only", /*toConsole*/ false);
      echoed = log.info("CON", "file and console", /*toConsole*/ true);
      flushed = log.flush();
      content = slurp(PATH);
    }
    const std::string CAPTURED = testing::internal::GetCapturedStdout();

    ASSERT_EQ(quiet, Status::OK);
    ASSERT_EQ(echoed, Status::OK);
    ASSERT_EQ(flushed, Status::OK);

    const auto LINES = splitLines(content);
    ASSERT_EQ(LINES.size(), 2u);
    EXPECT_EQ(LINES[0].substr(STAMP_LEN), " INFO: CON - file only");
    EXPECT_EQ(LINES[1].substr(STAMP_LEN), " INFO: CON - file and console");
    EXPECT_EQ(CAPTURED, LINES[1] + "\n");

    std::error_code ec;
    std::filesystem::remove(PATH, ec);
  }
}

/** @test Logs debug messages at or below the verbosity and skips those above it */
TEST(SystemLogTest, VerbosityFiltersDebugMessages) {
  const auto PATH = uniqTempFile("systemlog_verbosity");
  {
    SystemLog log(PATH.string());
    log.setLevel(SystemLog::Level::DEBUG);

    EXPECT_EQ(log.verbosity(), 0u);
    ASSERT_EQ(log.debug("SRC", "level zero at verbosity zero", 0), Status::OK);
    ASSERT_EQ(log.debug("SRC", "level one at verbosity zero", 1), Status::OK);

    log.setVerbosity(3);
    EXPECT_EQ(log.verbosity(), 3u);
    ASSERT_EQ(log.debug("SRC", "level three at verbosity three", 3), Status::OK);
    ASSERT_EQ(log.debug("SRC", "level four at verbosity three", 4), Status::OK);
  }
  const auto S = slurp(PATH);
  EXPECT_TRUE(contains(S, " DEBUG: SRC - level zero at verbosity zero\n"));
  EXPECT_FALSE(contains(S, "level one at verbosity zero"));
  EXPECT_TRUE(contains(S, " DEBUG: SRC - level three at verbosity three\n"));
  EXPECT_FALSE(contains(S, "level four at verbosity three"));

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

/** @test Writes the FATAL line with its error code in SYNC and in ASYNC mode */
TEST(SystemLogTest, FatalLineFormat) {
  for (const auto MODE : {SystemLog::Mode::SYNC, SystemLog::Mode::ASYNC}) {
    SCOPED_TRACE(MODE == SystemLog::Mode::ASYNC ? "ASYNC" : "SYNC");
    const auto PATH = uniqTempFile("systemlog_fatal");
    std::string content;
    {
      SystemLog log(PATH.string(), MODE);
      ASSERT_EQ(log.fatal("EXEC", /*ec*/ 42, "system halt", false), Status::OK);
      ASSERT_EQ(log.flush(), Status::OK);
      content = slurp(PATH);
    }
    EXPECT_TRUE(contains(content, " FATAL: EXEC[42] - system halt\n"));
    EXPECT_EQ(std::count(content.begin(), content.end(), '\n'), 1);

    std::error_code ec;
    std::filesystem::remove(PATH, ec);
  }
}

/** @test Falls back to SYNC and reports ERROR_OPEN when the log cannot be opened */
TEST(SystemLogTest, UnopenablePathFallsBackToSyncAndReportsOpenError) {
  const auto PATH = uniqTempFile("systemlog_no_such_dir").replace_extension() / "x.log";
  SystemLog log(PATH.string(), SystemLog::Mode::ASYNC);

  EXPECT_FALSE(log.isAsync());
  EXPECT_EQ(log.mode(), SystemLog::Mode::SYNC);
  EXPECT_EQ(log.lastOpenStatus(), Status::ERROR_OPEN);
  EXPECT_EQ(log.info("SRC", "nowhere to go", false), Status::ERROR_OPEN);
}

/** @test Reports and counts a failed write in SYNC mode */
TEST(SystemLogTest, SyncWriteFailureIsReportedAndCounted) {
  // A device that accepts the open and refuses every write: no space left.
  SystemLog log("/dev/full");
  ASSERT_EQ(log.lastOpenStatus(), Status::OK);

  EXPECT_EQ(log.writeErrorCount(), 0u);
  EXPECT_EQ(log.info("SRC", "does not fit", false), Status::ERROR_WRITE);
  EXPECT_EQ(log.writeErrorCount(), 1u);
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

/**
 * @test Pins the stated ASYNC limitation: rotate() repoints the SYNC descriptor
 *       only, so ASYNC entries keep going to the rotated file
 *
 * The backend writes through a descriptor of its own, which follows the file
 * into its backup name. This case states what an ASYNC log does on rotation
 * as it is documented; it changes with the design that gives ASYNC rotation
 * an owner.
 */
TEST(SystemLogTest, AsyncRotateLeavesEntriesInTheRotatedFile) {
  // A directory of its own keeps the live log and its backup together.
  const auto DIR = uniqTempFile("systemlog_async_rotate").replace_extension();
  std::error_code ec;
  std::filesystem::create_directory(DIR, ec);
  ASSERT_FALSE(ec) << "Failed to create temp dir: " << DIR;
  const auto PATH = DIR / "async.log";

  {
    SystemLog log(PATH.string(), SystemLog::Mode::ASYNC);
    ASSERT_TRUE(log.isAsync());
    ASSERT_EQ(log.info("ROT", "before rotation", false), Status::OK);
    ASSERT_EQ(log.flush(), Status::OK);

    ASSERT_EQ(log.rotate(0u), Status::OK);
    EXPECT_EQ(log.lastOpenStatus(), Status::OK);

    ASSERT_EQ(log.info("ROT", "after rotation", false), Status::OK);
    ASSERT_EQ(log.flush(), Status::OK);
  }

  // The rotation happened: the live file is a fresh one, and one backup
  // holds the line written before it. The line written after it is in the
  // backup too, and the live file is empty.
  std::vector<std::filesystem::path> backups;
  for (const auto& entry : std::filesystem::directory_iterator(DIR)) {
    if (entry.path() != PATH) {
      backups.push_back(entry.path());
    }
  }
  ASSERT_EQ(backups.size(), 1u);
  EXPECT_EQ(backups.front().extension(), ".backup");

  const auto LINES = splitLines(slurp(backups.front()));
  ASSERT_EQ(LINES.size(), 2u);
  EXPECT_EQ(LINES[0].substr(STAMP_LEN), " INFO: ROT - before rotation");
  EXPECT_EQ(LINES[1].substr(STAMP_LEN), " INFO: ROT - after rotation");
  EXPECT_EQ(slurp(PATH), "");
  EXPECT_TRUE(std::filesystem::exists(PATH));

  std::filesystem::remove_all(DIR, ec);
}

/** @test Keeps an ASYNC line longer than an entry on its own line */
TEST(SystemLogTest, AsyncLongLineStaysOneLine) {
  const auto PATH = uniqTempFile("systemlog_async_long");
  std::string content;
  {
    logs::SystemLog log(PATH.string(), logs::SystemLog::Mode::ASYNC);
    ASSERT_TRUE(log.isAsync());

    ASSERT_EQ(log.info("SRC", std::string(600, 'L'), false), Status::OK);
    ASSERT_EQ(log.info("SRC", "next", false), Status::OK);
    ASSERT_EQ(log.flush(), Status::OK);
    content = slurp(PATH);
  }

  ASSERT_EQ(std::count(content.begin(), content.end(), '\n'), 2);
  const auto FIRST_END = content.find('\n');
  EXPECT_EQ(FIRST_END, logs::LogEntry::MAX_MSG_LEN - 2);
  EXPECT_TRUE(contains(content.substr(0, FIRST_END), " INFO: SRC - LLLL"));
  const std::string SECOND = content.substr(FIRST_END + 1);
  EXPECT_EQ(SECOND.substr(SECOND.size() - 17), "INFO: SRC - next\n");

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

/** @test Has every earlier line and the FATAL line on disk when fatal returns with the hook on */
TEST(SystemLogTest, FatalFlushDrainsQueue) {
  const auto PATH = uniqTempFile("systemlog_fatal_flush");
  constexpr int PRE_FATAL = 64;
  std::string content;
  {
    logs::SystemLog log(PATH.string(), logs::SystemLog::Mode::ASYNC);
    log.setLevel(logs::SystemLog::Level::INFO);
    ASSERT_TRUE(log.isAsync());
    log.setFatalFlush(true);

    for (int i = 0; i < PRE_FATAL; ++i) {
      ASSERT_EQ(log.info("FF", "pre-fatal entry", false), logs::Status::OK);
    }
    ASSERT_EQ(log.fatal("FF", 42, "fatal with flush", false), logs::Status::OK);
    EXPECT_EQ(log.asyncBackend()->queueDepth(), 0u);
    content = slurp(PATH);
  }

  EXPECT_EQ(std::count(content.begin(), content.end(), '\n'), PRE_FATAL + 1);
  EXPECT_TRUE(contains(content, " FATAL: FF[42] - fatal with flush\n"));
  const std::string TAIL = " FATAL: FF[42] - fatal with flush\n";
  ASSERT_GE(content.size(), TAIL.size());
  EXPECT_EQ(content.substr(content.size() - TAIL.size()), TAIL);

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}
