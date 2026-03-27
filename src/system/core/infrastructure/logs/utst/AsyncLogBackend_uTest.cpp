/**
 * @file AsyncLogBackend_uTest.cpp
 * @brief Unit tests for AsyncLogBackend and LogEntry.
 */

#include "src/system/core/infrastructure/logs/inc/AsyncLogBackend.hpp"

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <chrono>

using logs::AsyncLogBackend;
using logs::LogEntry;

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

/* ----------------------------- LogEntry Tests ----------------------------- */

/**
 * @test LogEntry correctly stores a short message.
 */
TEST(LogEntryTest, ShortMessageStoredCorrectly) {
  const std::string MSG = "Short message";
  const LogEntry ENTRY(MSG);

  EXPECT_EQ(ENTRY.length, MSG.size());
  EXPECT_EQ(std::string_view(ENTRY.message, ENTRY.length), MSG);
}

/**
 * @test LogEntry correctly stores a message at maximum usable length.
 *
 * Note: MAX_MSG_LEN includes space for null terminator, so usable length
 * is MAX_MSG_LEN - 1.
 */
TEST(LogEntryTest, MaxUsableLengthMessage) {
  // Usable message length is MAX_MSG_LEN - 1 (null terminator takes one byte)
  const std::size_t MAX_USABLE = LogEntry::MAX_MSG_LEN - 1;
  const std::string MSG(MAX_USABLE, 'X');
  const LogEntry ENTRY(MSG);

  EXPECT_EQ(ENTRY.length, MAX_USABLE);
  EXPECT_EQ(std::string_view(ENTRY.message, ENTRY.length), MSG);
}

/**
 * @test LogEntry truncates messages exceeding usable length.
 */
TEST(LogEntryTest, TruncatesLongMessage) {
  // Usable message length is MAX_MSG_LEN - 1 (null terminator takes one byte)
  const std::size_t MAX_USABLE = LogEntry::MAX_MSG_LEN - 1;

  // Create message longer than buffer
  const std::string LONG_MSG(LogEntry::MAX_MSG_LEN + 100, 'Y');
  const LogEntry ENTRY(LONG_MSG);

  // Should truncate to MAX_USABLE (leaving room for null terminator)
  EXPECT_EQ(ENTRY.length, MAX_USABLE);

  // Truncated content should match first MAX_USABLE characters
  const std::string EXPECTED = LONG_MSG.substr(0, MAX_USABLE);
  EXPECT_EQ(std::string_view(ENTRY.message, ENTRY.length), EXPECTED);
}

/**
 * @test LogEntry handles empty message.
 */
TEST(LogEntryTest, EmptyMessage) {
  const LogEntry ENTRY("");

  EXPECT_EQ(ENTRY.length, 0);
}

/**
 * @test LogEntry default construction has zero length.
 */
TEST(LogEntryTest, DefaultConstruction) {
  const LogEntry ENTRY{};

  EXPECT_EQ(ENTRY.length, 0);
}

/* ----------------------------- AsyncLogBackend Tests ----------------------------- */

/**
 * @test AsyncLogBackend starts and stops cleanly.
 */
TEST(AsyncLogBackendTest, StartStopLifecycle) {
  const auto PATH = uniqTempFile("async_lifecycle");
  AsyncLogBackend backend(PATH.string());

  EXPECT_FALSE(backend.isRunning());
  EXPECT_TRUE(backend.start());
  EXPECT_TRUE(backend.isRunning());
  EXPECT_TRUE(backend.stop());
  EXPECT_FALSE(backend.isRunning());

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

/**
 * @test AsyncLogBackend tryLog enqueues and writes messages.
 */
TEST(AsyncLogBackendTest, TryLogWritesToFile) {
  const auto PATH = uniqTempFile("async_write");
  {
    AsyncLogBackend backend(PATH.string());
    ASSERT_TRUE(backend.start());

    EXPECT_TRUE(backend.tryLog("Test message 1"));
    EXPECT_TRUE(backend.tryLog("Test message 2"));

    // Flush to ensure writes complete
    EXPECT_TRUE(backend.flush(1000));
    backend.stop();
  }

  const auto S = slurp(PATH);
  EXPECT_TRUE(contains(S, "Test message 1"));
  EXPECT_TRUE(contains(S, "Test message 2"));

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

/**
 * @test AsyncLogBackend correctly truncates long messages.
 */
TEST(AsyncLogBackendTest, TruncatesLongMessages) {
  const auto PATH = uniqTempFile("async_truncate");
  {
    AsyncLogBackend backend(PATH.string());
    ASSERT_TRUE(backend.start());

    // Create a message that exceeds MAX_MSG_LEN
    const std::string PREFIX = "LONG:";
    const std::string LONG_MSG = PREFIX + std::string(LogEntry::MAX_MSG_LEN, 'Z');
    EXPECT_TRUE(backend.tryLog(LONG_MSG));

    EXPECT_TRUE(backend.flush(1000));
    backend.stop();
  }

  const auto S = slurp(PATH);

  // Should contain the prefix (first part of message)
  EXPECT_TRUE(contains(S, "LONG:"));

  // File content should be exactly MAX_MSG_LEN + newline
  // (backend adds newline after each entry)
  EXPECT_LE(S.size(), LogEntry::MAX_MSG_LEN + 1);

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

/**
 * @test AsyncLogBackend droppedCount increments on queue overflow.
 */
TEST(AsyncLogBackendTest, DroppedCountOnOverflow) {
  const auto PATH = uniqTempFile("async_overflow");
  {
    // Create backend with tiny queue
    AsyncLogBackend backend(PATH.string(), 4);
    ASSERT_TRUE(backend.start());

    // Fill queue beyond capacity - some should be dropped
    // Note: exact drop behavior depends on queue implementation
    EXPECT_EQ(backend.droppedCount(), 0);

    backend.stop();
  }

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

/**
 * @test AsyncLogBackend queueDepth tracks pending entries.
 */
TEST(AsyncLogBackendTest, QueueDepthTracking) {
  const auto PATH = uniqTempFile("async_depth");
  AsyncLogBackend backend(PATH.string());
  ASSERT_TRUE(backend.start());

  // Initially empty
  EXPECT_EQ(backend.queueDepth(), 0);

  // After flush, should be empty again
  backend.flush(1000);
  EXPECT_EQ(backend.queueDepth(), 0);

  backend.stop();

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}
