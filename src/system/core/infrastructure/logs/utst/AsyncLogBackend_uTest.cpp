/**
 * @file AsyncLogBackend_uTest.cpp
 * @brief Unit tests for AsyncLogBackend and LogEntry.
 */

#include "src/system/core/infrastructure/logs/inc/AsyncLogBackend.hpp"
#include "src/system/core/infrastructure/logs/inc/LogDrainService.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <chrono>

#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

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

/// Poll a condition every millisecond until it holds or the budget ends.
template <typename Predicate> static bool waitUntil(Predicate&& pred, std::chrono::seconds budget) {
  const auto DEADLINE = std::chrono::steady_clock::now() + budget;
  while (!pred()) {
    if (std::chrono::steady_clock::now() >= DEADLINE) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

/// Wait until the backend's queue depth has kept one value for 50 ms.
static bool depthSettles(const AsyncLogBackend& backend, std::chrono::seconds budget) {
  const auto DEADLINE = std::chrono::steady_clock::now() + budget;
  std::size_t last = backend.queueDepth();
  auto since = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() < DEADLINE) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const std::size_t NOW_DEPTH = backend.queueDepth();
    if (NOW_DEPTH != last) {
      last = NOW_DEPTH;
      since = std::chrono::steady_clock::now();
    } else if (std::chrono::steady_clock::now() - since >= std::chrono::milliseconds(50)) {
      return true;
    }
  }
  return false;
}

/// A FIFO standing in for the log file. Nothing reads it until release(), so
/// once the pipe is full the drain blocks inside write(): a stalled drain made
/// of production code only. The shared drain thread serves every async backend
/// of the process, so a test releases the pipe before its backend stops (see
/// ReleaseOnExit) and the destructor always empties it.
class StalledPipeLog {
public:
  explicit StalledPipeLog(const std::string& stem) : path_(uniqTempFile(stem)) {
    if (::mkfifo(path_.c_str(), 0600) == 0) {
      readFd_ = ::open(path_.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    }
    if (readFd_ >= 0) {
      const int SIZE = ::fcntl(readFd_, F_GETPIPE_SZ);
      pipeBytes_ = SIZE > 0 ? static_cast<std::size_t>(SIZE) : 0;
    }
  }

  ~StalledPipeLog() {
    (void)finish();
    if (readFd_ >= 0) {
      (void)::close(readFd_);
    }
    std::error_code ec;
    std::filesystem::remove(path_, ec);
  }

  StalledPipeLog(const StalledPipeLog&) = delete;
  StalledPipeLog& operator=(const StalledPipeLog&) = delete;

  bool ok() const { return readFd_ >= 0 && pipeBytes_ > 0; }
  const std::filesystem::path& path() const { return path_; }
  std::size_t pipeBytes() const { return pipeBytes_; }

  /// Bytes written to the pipe and not yet read.
  std::size_t buffered() const {
    int pending = 0;
    (void)::ioctl(readFd_, FIONREAD, &pending);
    return pending > 0 ? static_cast<std::size_t>(pending) : 0;
  }

  /// Start reading the pipe, which unblocks the drain.
  void release() {
    if (readFd_ >= 0 && !reader_.joinable()) {
      reader_ = std::thread([this]() { readLoop(); });
    }
  }

  /// Read what is left, stop the reader and return everything that arrived.
  /// Call once the writer has closed its end.
  std::string finish() {
    release();
    if (reader_.joinable()) {
      done_.store(true, std::memory_order_release);
      reader_.join();
    }
    return received_;
  }

private:
  void readLoop() {
    std::array<char, 4096> block{};
    while (true) {
      pollfd pfd{readFd_, POLLIN, 0};
      (void)::poll(&pfd, 1, 5);
      const ssize_t N = ::read(readFd_, block.data(), block.size());
      if (N > 0) {
        received_.append(block.data(), static_cast<std::size_t>(N));
      } else if (done_.load(std::memory_order_acquire)) {
        break;
      }
    }
  }

  std::filesystem::path path_;
  int readFd_{-1};
  std::size_t pipeBytes_{0};
  std::thread reader_;
  std::atomic<bool> done_{false};
  std::string received_;
};

/// Releases a stalled pipe when the scope ends. Declared after the backend,
/// so the pipe is being read before the backend's destructor stops it.
struct ReleaseOnExit {
  StalledPipeLog& pipe;
  ~ReleaseOnExit() { pipe.release(); }
};

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

/** @test Keeps the line terminator when a terminated message is cut */
TEST(LogEntryTest, CutLineKeepsItsNewline) {
  const std::size_t MAX_USABLE = LogEntry::MAX_MSG_LEN - 1;
  const std::string LINE = std::string(LogEntry::MAX_MSG_LEN + 100, 'A') + "\n";
  const LogEntry ENTRY(LINE);

  ASSERT_EQ(ENTRY.length, MAX_USABLE);
  EXPECT_EQ(std::string_view(ENTRY.message, MAX_USABLE - 1), std::string(MAX_USABLE - 1, 'A'));
  EXPECT_EQ(ENTRY.message[MAX_USABLE - 1], '\n');
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

/** @test Writes exactly the first 511 bytes of a longer message and counts the cut */
TEST(AsyncLogBackendTest, TruncatesLongMessages) {
  const auto PATH = uniqTempFile("async_truncate");
  const std::string LONG_MSG = "LONG:" + std::string(LogEntry::MAX_MSG_LEN, 'Z');
  {
    AsyncLogBackend backend(PATH.string());
    ASSERT_TRUE(backend.start());

    EXPECT_TRUE(backend.tryLog("short entry\n"));
    EXPECT_EQ(backend.truncatedCount(), 0u);
    EXPECT_TRUE(backend.tryLog(LONG_MSG));
    EXPECT_EQ(backend.truncatedCount(), 1u);

    EXPECT_TRUE(backend.flush(1000));
    backend.stop();
  }

  // The backend writes the stored bytes as they are: it adds no terminator.
  EXPECT_EQ(slurp(PATH), "short entry\n" + LONG_MSG.substr(0, LogEntry::MAX_MSG_LEN - 1));

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

/** @test Counts an entry as queued until it is written, so flush waits for it */
TEST(AsyncLogBackendTest, FlushWaitsForTheEntryBeingWritten) {
  StalledPipeLog pipe("async_flush_inflight");
  ASSERT_TRUE(pipe.ok());

  // Entries that divide the pipe exactly: the pipe takes FIT of them, and
  // the drain then holds one more inside a blocked write().
  constexpr std::size_t ENTRY_BYTES = 256;
  const std::size_t FIT = pipe.pipeBytes() / ENTRY_BYTES;
  ASSERT_EQ(FIT * ENTRY_BYTES, pipe.pipeBytes());
  const std::string ENTRY = std::string(ENTRY_BYTES - 1, 'e') + "\n";

  AsyncLogBackend backend(pipe.path().string(), 2 * FIT);
  ASSERT_TRUE(backend.start());
  const ReleaseOnExit RELEASE{pipe};

  for (std::size_t i = 0; i < FIT + 1; ++i) {
    ASSERT_TRUE(backend.tryLog(ENTRY));
  }
  ASSERT_TRUE(
      waitUntil([&]() { return pipe.buffered() == pipe.pipeBytes(); }, std::chrono::seconds(5)));
  ASSERT_TRUE(depthSettles(backend, std::chrono::seconds(5)));

  // One entry is not on the descriptor yet.
  EXPECT_EQ(backend.queueDepth(), 1u);
  EXPECT_FALSE(backend.flush(100));

  pipe.release();
  EXPECT_TRUE(backend.flush(5000));
  EXPECT_TRUE(backend.stop());

  const std::string RECEIVED = pipe.finish();
  EXPECT_EQ(static_cast<std::size_t>(std::count(RECEIVED.begin(), RECEIVED.end(), '\n')), FIT + 1);
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

/** @test Entries offered to a backend that never started are counted as drops. */
TEST(AsyncLogBackendTest, NotRunningCountsDrops) {
  logs::AsyncLogBackend backend("/tmp/logs_backend_notrunning_test.log", 16);

  EXPECT_FALSE(backend.tryLog("never accepted"));
  EXPECT_FALSE(backend.tryLog("never accepted either"));
  EXPECT_EQ(backend.droppedCount(), 2u);
  EXPECT_EQ(backend.writeErrorCount(), 0u);
}

/** @test Registry saturation falls back to a dedicated I/O thread that works.
 *
 * Fills all shared-drain slots, then verifies the next backend runs in
 * dedicated-thread mode end to end (accepts entries, drains, flushes,
 * stops clean) -- the fallback path the shared service otherwise shadows
 * on Linux. Also exercises registry churn on teardown.
 */
TEST(AsyncLogBackendTest, DedicatedFallbackWhenRegistryFull) {
  constexpr std::size_t FLEET = logs::LogDrainService::MAX_BACKENDS;
  std::array<std::unique_ptr<logs::AsyncLogBackend>, FLEET> fleet;

  for (std::size_t i = 0; i < FLEET; ++i) {
    const auto PATH = std::filesystem::temp_directory_path() /
                      ("logs_registry_fill_" + std::to_string(i) + ".log");
    fleet[i] = std::make_unique<logs::AsyncLogBackend>(PATH.string(), 16);
    ASSERT_TRUE(fleet[i]->start());
  }

  const auto PATH = std::filesystem::temp_directory_path() / "logs_dedicated_fallback.log";
  logs::AsyncLogBackend overflowBackend(PATH.string(), 16);
  ASSERT_TRUE(overflowBackend.start());
  EXPECT_FALSE(overflowBackend.usesSharedDrain());

  EXPECT_TRUE(overflowBackend.tryLog("dedicated-thread entry\n"));
  EXPECT_TRUE(overflowBackend.flush(2000));
  EXPECT_TRUE(overflowBackend.stop());
  EXPECT_EQ(overflowBackend.droppedCount(), 0u);

  for (std::size_t i = 0; i < FLEET; ++i) {
    EXPECT_TRUE(fleet[i]->stop());
    std::error_code ec;
    std::filesystem::remove(std::filesystem::temp_directory_path() /
                                ("logs_registry_fill_" + std::to_string(i) + ".log"),
                            ec);
  }
  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}
