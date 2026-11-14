/**
 * @file AsyncLogBackend_uTest.cpp
 * @brief Unit tests for AsyncLogBackend and LogEntry, and for SystemLog over
 *        a stalled drain (they share the stalled-pipe fixture).
 */

#include "src/system/core/infrastructure/logs/inc/AsyncLogBackend.hpp"
#include "src/system/core/infrastructure/logs/inc/LogDrainService.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

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

/// Holds every slot of the shared drain's registry, so a backend started
/// while it lives runs its own I/O thread: the dedicated path the shared
/// service otherwise shadows on Linux.
class RegistryFill {
public:
  RegistryFill() {
    for (std::size_t i = 0; i < FLEET; ++i) {
      paths_[i] = uniqTempFile("async_registry_fill");
      fleet_[i] = std::make_unique<AsyncLogBackend>(paths_[i].string(), 16);
      ok_ = fleet_[i]->start() && ok_;
    }
  }

  ~RegistryFill() {
    std::error_code ec;
    for (std::size_t i = 0; i < FLEET; ++i) {
      (void)fleet_[i]->stop();
      std::filesystem::remove(paths_[i], ec);
    }
  }

  RegistryFill(const RegistryFill&) = delete;
  RegistryFill& operator=(const RegistryFill&) = delete;

  bool ok() const { return ok_; }

private:
  static constexpr std::size_t FLEET = logs::LogDrainService::MAX_BACKENDS;
  std::array<std::unique_ptr<AsyncLogBackend>, FLEET> fleet_;
  std::array<std::filesystem::path, FLEET> paths_;
  bool ok_{true};
};

/// Which thread empties the backend under test.
enum class Drain { SHARED, DEDICATED };

/// The two steps of tryLog(), reachable so a test can stand between them:
/// where a producer preempted after admission and before publication is.
class TwoStepBackend : public AsyncLogBackend {
public:
  using AsyncLogBackend::admit;
  using AsyncLogBackend::AsyncLogBackend;
  using AsyncLogBackend::publishAdmitted;
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

/** @test Has every accepted entry in the file once flush returns true */
TEST(AsyncLogBackendTest, TryLogWritesToFile) {
  const auto PATH = uniqTempFile("async_write");
  {
    AsyncLogBackend backend(PATH.string());
    ASSERT_TRUE(backend.start());

    EXPECT_TRUE(backend.tryLog("Test message 1\n"));
    EXPECT_TRUE(backend.tryLog("Test message 2\n"));

    // Read before stop(): stop() drains on its own, so only this read
    // shows what flush() waited for.
    EXPECT_TRUE(backend.flush(1000));
    EXPECT_EQ(slurp(PATH), "Test message 1\nTest message 2\n");
    EXPECT_EQ(backend.queueDepth(), 0u);
    backend.stop();
  }

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

/// The drain holds the last accepted entry inside a blocked write():
/// flush() must not report completion until that write has been attempted.
static void expectFlushWaitsForTheEntryBeingWritten(Drain mode) {
  std::unique_ptr<RegistryFill> fill;
  if (mode == Drain::DEDICATED) {
    fill = std::make_unique<RegistryFill>();
    ASSERT_TRUE(fill->ok());
  }
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
  ASSERT_EQ(backend.usesSharedDrain(), mode == Drain::SHARED);
  const ReleaseOnExit RELEASE{pipe};

  for (std::size_t i = 0; i < FIT + 1; ++i) {
    ASSERT_TRUE(backend.tryLog(ENTRY));
  }
  // The pipe is full and the ring is empty: the last entry is in the drain's
  // hands, not yet on the descriptor.
  ASSERT_TRUE(
      waitUntil([&]() { return pipe.buffered() == pipe.pipeBytes() && backend.queueDepth() == 0; },
                std::chrono::seconds(5)));
  ASSERT_TRUE(depthSettles(backend, std::chrono::seconds(5)));

  EXPECT_FALSE(backend.flush(100));

  pipe.release();
  EXPECT_TRUE(backend.flush(5000));
  EXPECT_TRUE(backend.stop());

  const std::string RECEIVED = pipe.finish();
  EXPECT_EQ(static_cast<std::size_t>(std::count(RECEIVED.begin(), RECEIVED.end(), '\n')), FIT + 1);
}

/** @test Waits in flush for the entry the shared drain is still writing */
TEST(AsyncLogBackendTest, FlushWaitsForTheEntryBeingWrittenSharedDrain) {
  expectFlushWaitsForTheEntryBeingWritten(Drain::SHARED);
}

/** @test Waits in flush for the entry the dedicated I/O thread is still writing */
TEST(AsyncLogBackendTest, FlushWaitsForTheEntryBeingWrittenDedicatedThread) {
  expectFlushWaitsForTheEntryBeingWritten(Drain::DEDICATED);
}

/// Producer A is admitted and has not published, as when it is preempted
/// inside tryLog(); producer B logs and returns. B's line reaches the file,
/// and flush() still may not report completion until A has published and
/// its write was attempted: an admission is counted before publication, so
/// no release of another producer's entry can stand in for it.
static void expectFlushWaitsForAnAdmittedProducer(Drain mode) {
  std::unique_ptr<RegistryFill> fill;
  if (mode == Drain::DEDICATED) {
    fill = std::make_unique<RegistryFill>();
    ASSERT_TRUE(fill->ok());
  }
  const auto PATH = uniqTempFile("async_two_producers");
  {
    TwoStepBackend backend(PATH.string(), 16);
    ASSERT_TRUE(backend.start());
    ASSERT_EQ(backend.usesSharedDrain(), mode == Drain::SHARED);

    ASSERT_TRUE(backend.admit());
    ASSERT_TRUE(backend.tryLog("from producer B\n"));
    EXPECT_TRUE(
        waitUntil([&]() { return slurp(PATH) == "from producer B\n"; }, std::chrono::seconds(5)));

    EXPECT_FALSE(backend.flush(50));

    ASSERT_TRUE(backend.publishAdmitted("from producer A\n"));
    EXPECT_TRUE(backend.flush(2000));
    EXPECT_EQ(slurp(PATH), "from producer B\nfrom producer A\n");
    EXPECT_TRUE(backend.stop());
  }
  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

/** @test Holds flush for a producer admitted before another's entry was written, shared drain */
TEST(AsyncLogBackendTest, FlushWaitsForAnAdmittedProducerSharedDrain) {
  expectFlushWaitsForAnAdmittedProducer(Drain::SHARED);
}

/** @test Holds flush for a producer admitted before another's entry was written, dedicated thread
 */
TEST(AsyncLogBackendTest, FlushWaitsForAnAdmittedProducerDedicatedThread) {
  expectFlushWaitsForAnAdmittedProducer(Drain::DEDICATED);
}

/** @test Counts every entry a full queue refuses and delivers every accepted one */
TEST(AsyncLogBackendTest, FullQueueDropsAreCounted) {
  StalledPipeLog pipe("async_overflow");
  ASSERT_TRUE(pipe.ok());

  constexpr std::size_t ENTRY_BYTES = 256;
  constexpr std::size_t CAPACITY = 8;
  ASSERT_EQ(pipe.pipeBytes() % ENTRY_BYTES, 0u);
  const std::string ENTRY = std::string(ENTRY_BYTES - 1, 'o') + "\n";

  AsyncLogBackend backend(pipe.path().string(), CAPACITY);
  ASSERT_TRUE(backend.start());
  const ReleaseOnExit RELEASE{pipe};

  std::size_t accepted = 0;
  std::size_t refused = 0;
  const auto OFFER = [&]() { backend.tryLog(ENTRY) ? ++accepted : ++refused; };

  // Fill the pipe, pacing the offers so the small ring keeps up.
  ASSERT_TRUE(waitUntil(
      [&]() {
        if (pipe.buffered() == pipe.pipeBytes()) {
          return true;
        }
        OFFER();
        return false;
      },
      std::chrono::seconds(10)));

  // The drain is stalled: the queue fills and everything after is refused.
  // The entry in the drain's hands has left the depth, so the ring settles
  // full or one short of it, depending on when the drain took that entry.
  for (std::size_t i = 0; i < 4 * CAPACITY; ++i) {
    OFFER();
  }
  ASSERT_TRUE(depthSettles(backend, std::chrono::seconds(5)));
  EXPECT_GE(backend.queueDepth(), CAPACITY - 1);
  EXPECT_LE(backend.queueDepth(), CAPACITY);
  EXPECT_GE(refused, 2 * CAPACITY);
  EXPECT_EQ(backend.droppedCount(), refused);
  EXPECT_FALSE(backend.flush(10));

  pipe.release();
  EXPECT_TRUE(backend.flush(5000));
  EXPECT_EQ(backend.queueDepth(), 0u);
  EXPECT_TRUE(backend.stop());

  const std::string RECEIVED = pipe.finish();
  EXPECT_EQ(static_cast<std::size_t>(std::count(RECEIVED.begin(), RECEIVED.end(), '\n')), accepted);
  EXPECT_EQ(RECEIVED.size(), accepted * ENTRY_BYTES);
}

/** @test Writes nothing for an empty entry and reports an empty queue after it */
TEST(AsyncLogBackendTest, EmptyEntryWritesNothing) {
  const auto PATH = uniqTempFile("async_empty");
  AsyncLogBackend backend(PATH.string());
  ASSERT_TRUE(backend.start());

  EXPECT_TRUE(backend.tryLog(""));
  EXPECT_TRUE(backend.flush(1000));
  EXPECT_EQ(backend.queueDepth(), 0u);
  EXPECT_EQ(slurp(PATH), "");
  EXPECT_TRUE(backend.stop());

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

/** @test Refuses a second start and treats stop and flush as done once stopped */
TEST(AsyncLogBackendTest, LifecycleCallsOutOfOrder) {
  const auto PATH = uniqTempFile("async_lifecycle_negative");
  AsyncLogBackend backend(PATH.string());

  EXPECT_TRUE(backend.stop());
  EXPECT_TRUE(backend.flush(10));

  ASSERT_TRUE(backend.start());
  EXPECT_FALSE(backend.start());
  EXPECT_TRUE(backend.isRunning());

  EXPECT_TRUE(backend.stop());
  EXPECT_TRUE(backend.stop());
  EXPECT_TRUE(backend.flush(10));

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

/** @test Fails to start when the log cannot be opened and counts later entries as drops */
TEST(AsyncLogBackendTest, UnopenablePathDoesNotStart) {
  const auto PATH = uniqTempFile("async_no_such_dir").replace_extension() / "x.log";
  AsyncLogBackend backend(PATH.string());

  EXPECT_FALSE(backend.start());
  EXPECT_FALSE(backend.isRunning());
  EXPECT_FALSE(backend.tryLog("nowhere to go\n"));
  EXPECT_EQ(backend.droppedCount(), 1u);
}

/** @test Counts a write the drain could not complete */
TEST(AsyncLogBackendTest, DrainWriteFailureIsCounted) {
  // A device that accepts the open and refuses every write: no space left.
  AsyncLogBackend backend("/dev/full");
  ASSERT_TRUE(backend.start());

  EXPECT_TRUE(backend.tryLog("does not fit\n"));
  EXPECT_TRUE(backend.flush(1000));
  EXPECT_EQ(backend.writeErrorCount(), 1u);
  EXPECT_EQ(backend.droppedCount(), 0u);
  EXPECT_TRUE(backend.stop());
}

/** @test Writes every accepted entry when stop follows a burst at once */
TEST(AsyncLogBackendTest, StopAfterBurstWritesEveryEntry) {
  const auto PATH = uniqTempFile("async_burst_stop");
  constexpr std::size_t BURST = 2000;
  std::size_t accepted = 0;
  {
    AsyncLogBackend backend(PATH.string());
    ASSERT_TRUE(backend.start());
    for (std::size_t i = 0; i < BURST; ++i) {
      if (backend.tryLog("burst entry\n")) {
        ++accepted;
      }
    }
    EXPECT_TRUE(backend.stop());
    EXPECT_EQ(backend.droppedCount(), BURST - accepted);
  }
  EXPECT_EQ(accepted, BURST);
  const auto S = slurp(PATH);
  EXPECT_EQ(static_cast<std::size_t>(std::count(S.begin(), S.end(), '\n')), accepted);

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

/** @test Stops a running backend in its destructor and leaves its entries on disk */
TEST(AsyncLogBackendTest, DestructorStopsAndWrites) {
  const auto PATH = uniqTempFile("async_destructor");
  {
    AsyncLogBackend backend(PATH.string());
    ASSERT_TRUE(backend.start());
    EXPECT_TRUE(backend.tryLog("left to the destructor\n"));
  }
  EXPECT_EQ(slurp(PATH), "left to the destructor\n");

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

/** @test Entries offered to a backend that never started are counted as drops. */
TEST(AsyncLogBackendTest, NotRunningCountsDrops) {
  logs::AsyncLogBackend backend(uniqTempFile("async_not_running").string(), 16);

  EXPECT_FALSE(backend.tryLog("never accepted"));
  EXPECT_FALSE(backend.tryLog("never accepted either"));
  EXPECT_EQ(backend.droppedCount(), 2u);
  EXPECT_EQ(backend.writeErrorCount(), 0u);
}

/** @test Writes through a dedicated I/O thread when the shared drain's registry is full */
TEST(AsyncLogBackendTest, DedicatedFallbackWhenRegistryFull) {
  const RegistryFill FILL;
  ASSERT_TRUE(FILL.ok());

  const auto PATH = uniqTempFile("async_dedicated_fallback");
  logs::AsyncLogBackend overflowBackend(PATH.string(), 16);
  ASSERT_TRUE(overflowBackend.start());
  EXPECT_FALSE(overflowBackend.usesSharedDrain());

  EXPECT_TRUE(overflowBackend.tryLog("dedicated-thread entry\n"));
  EXPECT_TRUE(overflowBackend.flush(2000));
  EXPECT_EQ(slurp(PATH), "dedicated-thread entry\n");
  EXPECT_TRUE(overflowBackend.stop());
  EXPECT_EQ(overflowBackend.droppedCount(), 0u);

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

/* ----------------------------- SystemLog Over A Stalled Drain ----------------------------- */

/** @test Returns OK from info and counts the entry when the async queue refuses it */
TEST(SystemLogAsyncTest, FullQueueDropIsCountedAndInfoReturnsOk) {
  StalledPipeLog pipe("systemlog_async_overflow");
  ASSERT_TRUE(pipe.ok());

  std::size_t offered = 0;
  std::uint64_t dropped = 0;
  {
    logs::SystemLog log(pipe.path().string(), logs::SystemLog::Mode::ASYNC, 8);
    ASSERT_TRUE(log.isAsync());
    const ReleaseOnExit RELEASE{pipe};
    const auto BACKEND = log.asyncBackend();
    const std::string MSG(200, 'd');

    // The producer outruns the drain of an eight-entry queue, and once the
    // unread pipe is full the drain stops altogether: entries are refused.
    const auto DEADLINE = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (BACKEND->droppedCount() < 16 && std::chrono::steady_clock::now() < DEADLINE) {
      ASSERT_EQ(log.info("SRC", MSG, false), logs::Status::OK);
      ++offered;
    }
    dropped = BACKEND->droppedCount();
    EXPECT_GE(dropped, 16u);

    pipe.release();
    EXPECT_EQ(log.flush(), logs::Status::OK);
    EXPECT_EQ(BACKEND->droppedCount(), dropped);
  }

  const std::string RECEIVED = pipe.finish();
  EXPECT_EQ(static_cast<std::uint64_t>(std::count(RECEIVED.begin(), RECEIVED.end(), '\n')),
            offered - dropped);
}

/** @test Reports ERROR_SYNC from flush when the async queue does not drain in time */
TEST(SystemLogAsyncTest, FlushTimeoutReportsSyncError) {
  StalledPipeLog pipe("systemlog_async_flush_timeout");
  ASSERT_TRUE(pipe.ok());

  logs::SystemLog log(pipe.path().string(), logs::SystemLog::Mode::ASYNC, 64);
  ASSERT_TRUE(log.isAsync());
  const ReleaseOnExit RELEASE{pipe};
  const auto BACKEND = log.asyncBackend();
  const std::string MSG(200, 't');

  // Offer lines until the drain has stopped: entries are waiting and the
  // pipe has not grown for 50 ms. (Formatted lines do not divide a pipe
  // page, so the pipe is full a little below its nominal size.)
  ASSERT_TRUE(waitUntil(
      [&]() {
        for (int i = 0; i < 32; ++i) {
          (void)log.info("SRC", MSG, false);
        }
        const std::size_t BEFORE = pipe.buffered();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return BACKEND->queueDepth() > 0 && pipe.buffered() == BEFORE;
      },
      std::chrono::seconds(10)));

  EXPECT_EQ(log.flush(10), logs::Status::ERROR_SYNC);

  pipe.release();
  EXPECT_EQ(log.flush(), logs::Status::OK);
  EXPECT_EQ(BACKEND->queueDepth(), 0u);
}
