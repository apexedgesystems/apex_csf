/**
 * @file LinuxPps_uTest.cpp
 * @brief Unit tests for LinuxPps.
 *
 * Real /dev/pps[N] devices are not assumed to exist on the host running
 * these tests (CI runners don't have one). All ioctl, open, close, and
 * clock_gettime calls are routed through protected virtual seams so tests
 * can inject fake behavior. The InMemoryLinuxPps subclass below stages
 * sequence numbers and timestamps that simulate a kernel-latched edge.
 */

#include "src/system/core/hal/linux/inc/LinuxPps.hpp"

#include <gtest/gtest.h>

#include <cerrno>
#include <cstring>
#include <linux/pps.h>
#include <vector>

using apex::hal::LinuxPps;
using apex::hal::PpsConfig;
using apex::hal::PpsEdge;
using apex::hal::PpsStatus;

namespace {

/// Fake LinuxPps that stages sequence/timestamp pairs. Tests call
/// pushEdge() to enqueue what the next ioctl should report.
class InMemoryLinuxPps : public LinuxPps {
public:
  using LinuxPps::LinuxPps;

  /// One staged "kernel state" the next ioctl will return.
  struct StagedEdge {
    uint32_t assertSeq = 0;
    uint32_t clearSeq = 0;
  };

  /// Stage a single PPS_FETCH response.
  void pushEdge(const StagedEdge& edge) { stagedEdges_.push_back(edge); }

  /// Stage many edges with monotonically increasing sequence numbers.
  void pushAssertSeries(uint32_t startSeq, size_t count) {
    for (size_t i = 0; i < count; ++i) {
      stagedEdges_.push_back({static_cast<uint32_t>(startSeq + i), 0});
    }
  }

  /// Force the next sysIoctl call to fail.
  void failNextIoctl() { ioctlFailures_ += 1; }

  /// Force sysOpen to fail (returning -1 with errno=ENOENT).
  void failOpen() { openShouldFail_ = true; }

  /// Force the next sysClockGettime call to fail.
  void failNextClockGettime() { clockFailures_ += 1; }

  /// Set the timespec returned by the next sysClockGettime call.
  void setNextMonotonic(int64_t sec, int64_t nsec) {
    nextMonotonic_.tv_sec = sec;
    nextMonotonic_.tv_nsec = nsec;
    haveStagedMonotonic_ = true;
  }

  /// Number of times sysClose was invoked.
  int closeCount() const { return closeCount_; }

protected:
  int sysOpen(const char* path, int flags) noexcept override {
    (void)path;
    (void)flags;
    if (openShouldFail_) {
      errno = ENOENT;
      return -1;
    }
    return ++fakeFdCounter_; // any positive fake fd
  }

  int sysClose(int /*fd*/) noexcept override {
    ++closeCount_;
    return 0;
  }

  int sysIoctl(int /*fd*/, unsigned long request, void* argp) noexcept override {
    if (ioctlFailures_ > 0) {
      --ioctlFailures_;
      errno = EIO;
      return -1;
    }
    if (request != PPS_FETCH || argp == nullptr) {
      errno = EINVAL;
      return -1;
    }
    auto* fdata = static_cast<pps_fdata*>(argp);
    std::memset(fdata, 0, sizeof(*fdata));

    if (!stagedEdges_.empty()) {
      const StagedEdge edge = stagedEdges_.front();
      stagedEdges_.erase(stagedEdges_.begin());
      fdata->info.assert_sequence = edge.assertSeq;
      fdata->info.clear_sequence = edge.clearSeq;
    }
    // No staged edges: return zeroed fdata (no edge ever observed).
    return 0;
  }

  int sysClockGettime(clockid_t /*clkId*/, struct timespec* tsOut) noexcept override {
    if (clockFailures_ > 0) {
      --clockFailures_;
      errno = EINVAL;
      return -1;
    }
    if (haveStagedMonotonic_) {
      *tsOut = nextMonotonic_;
      haveStagedMonotonic_ = false;
      // Auto-advance for next call so back-to-back edges have distinct
      // timestamps unless the test re-stages.
      nextMonotonic_.tv_sec += 1;
    } else {
      // Default: a deterministic monotonically increasing time.
      static int64_t seqCounter = 0;
      tsOut->tv_sec = ++seqCounter;
      tsOut->tv_nsec = 0;
    }
    return 0;
  }

private:
  std::vector<StagedEdge> stagedEdges_;
  int ioctlFailures_ = 0;
  int clockFailures_ = 0;
  bool openShouldFail_ = false;
  int fakeFdCounter_ = 100;
  int closeCount_ = 0;
  struct timespec nextMonotonic_{};
  bool haveStagedMonotonic_ = false;
};

} // namespace

/* ----------------------------- Lifecycle ----------------------------- */

/** @test Default construction does not touch the device. */
TEST(LinuxPps, DefaultUninitialized) {
  InMemoryLinuxPps pps("/dev/pps0");
  EXPECT_FALSE(pps.isInitialized());
  EXPECT_EQ(pps.fd(), -1);
  EXPECT_STREQ(pps.devicePath(), "/dev/pps0");
  EXPECT_EQ(pps.pulseCount(), 0U);
}

/** @test init succeeds against the fake open(). */
TEST(LinuxPps, InitSucceeds) {
  InMemoryLinuxPps pps("/dev/pps0");
  EXPECT_EQ(pps.init({}), PpsStatus::OK);
  EXPECT_TRUE(pps.isInitialized());
  EXPECT_GT(pps.fd(), 0);
}

/** @test init fails when sysOpen fails (e.g. /dev/pps0 missing). */
TEST(LinuxPps, InitFailsWhenOpenFails) {
  InMemoryLinuxPps pps("/dev/pps_does_not_exist");
  pps.failOpen();
  EXPECT_EQ(pps.init({}), PpsStatus::ERROR_DEVICE);
  EXPECT_FALSE(pps.isInitialized());
  EXPECT_EQ(pps.fd(), -1);
}

/** @test init with null path returns ERROR_INVALID_ARG. */
TEST(LinuxPps, InitFailsOnNullPath) {
  InMemoryLinuxPps pps(nullptr);
  EXPECT_EQ(pps.init({}), PpsStatus::ERROR_INVALID_ARG);
}

/** @test deinit closes the underlying fd exactly once. */
TEST(LinuxPps, DeinitClosesFd) {
  InMemoryLinuxPps pps("/dev/pps0");
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.deinit();
  EXPECT_FALSE(pps.isInitialized());
  EXPECT_EQ(pps.fd(), -1);
  EXPECT_EQ(pps.closeCount(), 1);
}

/** @test deinit on a never-initialized object is a no-op. */
TEST(LinuxPps, DeinitWithoutInitNoOp) {
  InMemoryLinuxPps pps("/dev/pps0");
  pps.deinit();
  EXPECT_EQ(pps.closeCount(), 0);
}

/* ----------------------------- readCapture ----------------------------- */

/** @test readCapture before init returns ERROR_NOT_INIT. */
TEST(LinuxPps, ReadCaptureBeforeInit) {
  InMemoryLinuxPps pps("/dev/pps0");
  int64_t ts = 0;
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::ERROR_NOT_INIT);
}

/** @test First readCapture after init primes the baseline without reporting
 *        an edge (kernel sequence may already be non-zero from before init). */
TEST(LinuxPps, FirstReadPrimesBaseline) {
  InMemoryLinuxPps pps("/dev/pps0");
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.pushEdge({.assertSeq = 5, .clearSeq = 0}); // any non-zero seq

  int64_t ts = 0;
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::NO_NEW_EDGE);
  EXPECT_EQ(pps.pulseCount(), 0U); // baseline does not count as an edge
  EXPECT_EQ(pps.lastSequence(), 5U);
}

/** @test Subsequent reads with unchanged seq return NO_NEW_EDGE. */
TEST(LinuxPps, SameSequenceIsNoNewEdge) {
  InMemoryLinuxPps pps("/dev/pps0");
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.pushEdge({.assertSeq = 7, .clearSeq = 0});
  pps.pushEdge({.assertSeq = 7, .clearSeq = 0});

  int64_t ts = 0;
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::NO_NEW_EDGE); // primes
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::NO_NEW_EDGE); // same seq
  EXPECT_EQ(pps.pulseCount(), 0U);
}

/** @test Sequence increment delivers an edge with the monotonic timestamp. */
TEST(LinuxPps, EdgeReportsMonotonicTimestamp) {
  InMemoryLinuxPps pps("/dev/pps0");
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.pushEdge({.assertSeq = 1, .clearSeq = 0}); // primes
  pps.pushEdge({.assertSeq = 2, .clearSeq = 0}); // edge

  int64_t ts = 0;
  ASSERT_EQ(pps.readCapture(ts), PpsStatus::NO_NEW_EDGE);

  pps.setNextMonotonic(123, 456'789'000);
  ASSERT_EQ(pps.readCapture(ts), PpsStatus::OK);
  EXPECT_EQ(ts, 123 * 1'000'000'000LL + 456'789'000LL);
  EXPECT_EQ(pps.pulseCount(), 1U);
  EXPECT_EQ(pps.stats().captureCount, 1U);
}

/** @test Multiple new edges accumulate correctly into pulseCount. */
TEST(LinuxPps, GapDeliversMultiplePulses) {
  InMemoryLinuxPps pps("/dev/pps0");
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.pushEdge({.assertSeq = 10, .clearSeq = 0}); // primes baseline at 10
  pps.pushEdge({.assertSeq = 13, .clearSeq = 0}); // 3 edges since baseline

  int64_t ts = 0;
  ASSERT_EQ(pps.readCapture(ts), PpsStatus::NO_NEW_EDGE);
  ASSERT_EQ(pps.readCapture(ts), PpsStatus::OK);
  EXPECT_EQ(pps.pulseCount(), 3U);
}

/** @test FALLING-edge config reads from clear_sequence instead. */
TEST(LinuxPps, FallingEdgeUsesClearSequence) {
  InMemoryLinuxPps pps("/dev/pps0");
  PpsConfig cfg;
  cfg.edge = PpsEdge::FALLING;
  ASSERT_EQ(pps.init(cfg), PpsStatus::OK);
  pps.pushEdge({.assertSeq = 100, .clearSeq = 1}); // primes on clear=1
  pps.pushEdge({.assertSeq = 100, .clearSeq = 2}); // edge on clear

  int64_t ts = 0;
  ASSERT_EQ(pps.readCapture(ts), PpsStatus::NO_NEW_EDGE);
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::OK);
  EXPECT_EQ(pps.pulseCount(), 1U);
}

/* ----------------------------- Error paths ----------------------------- */

/** @test ioctl failure surfaces as ERROR_DEVICE and increments errorCount. */
TEST(LinuxPps, IoctlFailureReportsErrorDevice) {
  InMemoryLinuxPps pps("/dev/pps0");
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.failNextIoctl();

  int64_t ts = 0;
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::ERROR_DEVICE);
  EXPECT_EQ(pps.stats().errorCount, 1U);
  EXPECT_EQ(pps.stats().captureCount, 0U);
}

/** @test clock_gettime failure on an edge surfaces as ERROR_DEVICE. */
TEST(LinuxPps, ClockGettimeFailureReportsErrorDevice) {
  InMemoryLinuxPps pps("/dev/pps0");
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.pushEdge({.assertSeq = 1, .clearSeq = 0}); // primes
  pps.pushEdge({.assertSeq = 2, .clearSeq = 0}); // edge

  int64_t ts = 0;
  ASSERT_EQ(pps.readCapture(ts), PpsStatus::NO_NEW_EDGE);
  pps.failNextClockGettime();
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::ERROR_DEVICE);
  EXPECT_EQ(pps.stats().errorCount, 1U);
}

/* ----------------------------- Re-init ----------------------------- */

/** @test init/deinit/init re-opens cleanly and resets state. */
TEST(LinuxPps, ReInitResetsState) {
  InMemoryLinuxPps pps("/dev/pps0");
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.pushEdge({.assertSeq = 1, .clearSeq = 0});
  pps.pushEdge({.assertSeq = 2, .clearSeq = 0});

  int64_t ts = 0;
  ASSERT_EQ(pps.readCapture(ts), PpsStatus::NO_NEW_EDGE);
  ASSERT_EQ(pps.readCapture(ts), PpsStatus::OK);
  ASSERT_EQ(pps.pulseCount(), 1U);

  pps.deinit();
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  EXPECT_EQ(pps.pulseCount(), 0U);
  EXPECT_EQ(pps.stats().captureCount, 0U);
}
