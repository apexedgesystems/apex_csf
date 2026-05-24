/**
 * @file LinController_pTest.cpp
 * @brief Performance tests for LIN protocol frame and transaction paths.
 *
 * Measures:
 *  - Frame header / response / full-frame building overhead
 *  - PID parity calculate and verify throughput
 *  - Classic and enhanced checksum calculate + verify
 *  - Frame and response parsing
 *  - PTY-loopback transaction latency via LinController
 *
 * Usage:
 *   ./LinController_PTEST --csv results.csv
 *   ./LinController_PTEST --quick
 *   ./LinController_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/lin/inc/LinController.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/lin/inc/LinFrame.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/PtyPair.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartAdapter.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

namespace ub = vernier::bench;
namespace lin = apex::protocols::fieldbus::lin;
namespace uart = apex::protocols::serial::uart;

/* ----------------------------- Local Helpers ----------------------------- */

namespace {

inline const ub::PerfConfig& getCfg() { return ub::detail::getPerfConfig(); }

} // namespace

/* ----------------------------- Frame Building ----------------------------- */

/**
 * @brief Frame header build overhead (FrameBuffer + PID + byte packing).
 */
PERF_TEST(LinFrameBuild, Header) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  lin::FrameBuffer buffer;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      buffer.reset();
      volatile auto s = lin::buildHeader(buffer, 0x10);
      (void)s;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        buffer.reset();
        auto s = lin::buildHeader(buffer, 0x10);
        ASSERT_EQ(s, lin::Status::SUCCESS);
      },
      "build-header");

  std::printf("\nBuild Header: %.3f us (%.0f frames/s)\n", R.stats.median, R.callsPerSecond);
}

/**
 * @brief Response build overhead for an 8-byte payload (enhanced checksum).
 */
PERF_TEST(LinFrameBuild, Response8Bytes) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  const std::uint8_t PID = lin::calculatePid(0x10);
  const std::array<std::uint8_t, 8> DATA = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  lin::FrameBuffer buffer;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      buffer.reset();
      volatile auto s =
          lin::buildResponse(buffer, PID, DATA.data(), DATA.size(), lin::ChecksumType::ENHANCED);
      (void)s;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        buffer.reset();
        auto s =
            lin::buildResponse(buffer, PID, DATA.data(), DATA.size(), lin::ChecksumType::ENHANCED);
        ASSERT_EQ(s, lin::Status::SUCCESS);
      },
      "build-response-8b");

  std::printf("\nBuild Response (8B): %.3f us (%.0f frames/s)\n", R.stats.median, R.callsPerSecond);
}

/**
 * @brief Full frame build (header + data + checksum).
 */
PERF_TEST(LinFrameBuild, FullFrame) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  const std::array<std::uint8_t, 4> DATA = {0xDE, 0xAD, 0xBE, 0xEF};
  lin::FrameBuffer buffer;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      buffer.reset();
      volatile auto s =
          lin::buildFrame(buffer, 0x20, DATA.data(), DATA.size(), lin::ChecksumType::ENHANCED);
      (void)s;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        buffer.reset();
        auto s =
            lin::buildFrame(buffer, 0x20, DATA.data(), DATA.size(), lin::ChecksumType::ENHANCED);
        ASSERT_EQ(s, lin::Status::SUCCESS);
      },
      "build-full-frame");

  std::printf("\nBuild Full Frame (4B data): %.3f us (%.0f frames/s)\n", R.stats.median,
              R.callsPerSecond);
}

/* ----------------------------- PID Calculation ----------------------------- */

/**
 * @brief PID parity calculation across all 64 IDs.
 */
PERF_TEST(LinPid, Calculate) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto pid = lin::calculatePid(static_cast<std::uint8_t>(i & 0x3F));
      (void)pid;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        for (std::uint8_t id = 0; id < 64; ++id) {
          volatile auto pid = lin::calculatePid(id);
          (void)pid;
        }
      },
      "pid-calc-all-ids");

  const double NS_PER_CALC = (R.stats.median * 1000.0) / 64.0;
  std::printf("\nPID calculation (all 64 IDs): %.3f us total, %.1f ns/calc\n", R.stats.median,
              NS_PER_CALC);
}

/**
 * @brief PID parity verification across all 64 IDs.
 */
PERF_TEST(LinPid, Verify) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  std::array<std::uint8_t, 64> pids{};
  for (std::uint8_t i = 0; i < 64; ++i) {
    pids[i] = lin::calculatePid(i);
  }

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto valid = lin::verifyPidParity(pids[i % 64]);
      (void)valid;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        for (const auto pid : pids) {
          volatile auto valid = lin::verifyPidParity(pid);
          (void)valid;
        }
      },
      "pid-verify-all");

  const double NS_PER_VERIFY = (R.stats.median * 1000.0) / 64.0;
  std::printf("\nPID verification (all 64): %.3f us total, %.1f ns/verify\n", R.stats.median,
              NS_PER_VERIFY);
}

/* ----------------------------- Checksum Calculation ----------------------------- */

/**
 * @brief Classic checksum (LIN 1.x) over 8 bytes.
 */
PERF_TEST(LinChecksum, Classic) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  const std::array<std::uint8_t, 8> DATA = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  const std::uint8_t PID = lin::calculatePid(0x10);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto cs =
          lin::calculateChecksum(DATA.data(), DATA.size(), PID, lin::ChecksumType::CLASSIC);
      (void)cs;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        volatile auto cs =
            lin::calculateChecksum(DATA.data(), DATA.size(), PID, lin::ChecksumType::CLASSIC);
        (void)cs;
      },
      "checksum-classic-8b");

  std::printf("\nClassic checksum (8B): %.3f us (%.0f calcs/s)\n", R.stats.median,
              R.callsPerSecond);
}

/**
 * @brief Enhanced checksum (LIN 2.x) over 8 bytes.
 */
PERF_TEST(LinChecksum, Enhanced) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  const std::array<std::uint8_t, 8> DATA = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  const std::uint8_t PID = lin::calculatePid(0x10);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto cs =
          lin::calculateChecksum(DATA.data(), DATA.size(), PID, lin::ChecksumType::ENHANCED);
      (void)cs;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        volatile auto cs =
            lin::calculateChecksum(DATA.data(), DATA.size(), PID, lin::ChecksumType::ENHANCED);
        (void)cs;
      },
      "checksum-enhanced-8b");

  std::printf("\nEnhanced checksum (8B): %.3f us (%.0f calcs/s)\n", R.stats.median,
              R.callsPerSecond);
}

/**
 * @brief Checksum verification (enhanced) over 8 bytes.
 */
PERF_TEST(LinChecksum, Verify) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  const std::array<std::uint8_t, 8> DATA = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  const std::uint8_t PID = lin::calculatePid(0x10);
  const std::uint8_t CHECKSUM =
      lin::calculateChecksum(DATA.data(), DATA.size(), PID, lin::ChecksumType::ENHANCED);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto valid =
          lin::verifyChecksum(DATA.data(), DATA.size(), CHECKSUM, PID, lin::ChecksumType::ENHANCED);
      (void)valid;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        auto valid = lin::verifyChecksum(DATA.data(), DATA.size(), CHECKSUM, PID,
                                         lin::ChecksumType::ENHANCED);
        ASSERT_TRUE(valid);
      },
      "checksum-verify-8b");

  std::printf("\nChecksum verification (8B): %.3f us (%.0f verifies/s)\n", R.stats.median,
              R.callsPerSecond);
}

/* ----------------------------- Frame Parsing ----------------------------- */

/**
 * @brief Full frame parse throughput.
 */
PERF_TEST(LinParse, FullFrame) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  lin::FrameBuffer buffer;
  const std::array<std::uint8_t, 4> DATA = {0xDE, 0xAD, 0xBE, 0xEF};
  ASSERT_EQ(lin::buildFrame(buffer, 0x20, DATA.data(), DATA.size(), lin::ChecksumType::ENHANCED),
            lin::Status::SUCCESS);

  lin::ParsedFrame parsed;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto s =
          lin::parseFrame(buffer.ptr(), buffer.length, lin::ChecksumType::ENHANCED, parsed);
      (void)s;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        auto s = lin::parseFrame(buffer.ptr(), buffer.length, lin::ChecksumType::ENHANCED, parsed);
        ASSERT_EQ(s, lin::Status::SUCCESS);
      },
      "parse-full-frame");

  std::printf("\nParse Frame (4B data): %.3f us (%.0f parses/s)\n", R.stats.median,
              R.callsPerSecond);
}

/**
 * @brief Response parse throughput.
 */
PERF_TEST(LinParse, Response) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  const std::uint8_t PID = lin::calculatePid(0x30);
  lin::FrameBuffer buffer;
  const std::array<std::uint8_t, 8> DATA = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  ASSERT_EQ(lin::buildResponse(buffer, PID, DATA.data(), DATA.size(), lin::ChecksumType::ENHANCED),
            lin::Status::SUCCESS);

  lin::ParsedFrame parsed;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto s = lin::parseResponse(buffer.ptr(), buffer.length, PID, DATA.size(),
                                           lin::ChecksumType::ENHANCED, parsed);
      (void)s;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        auto s = lin::parseResponse(buffer.ptr(), buffer.length, PID, DATA.size(),
                                    lin::ChecksumType::ENHANCED, parsed);
        ASSERT_EQ(s, lin::Status::SUCCESS);
      },
      "parse-response-8b");

  std::printf("\nParse Response (8B): %.3f us (%.0f parses/s)\n", R.stats.median, R.callsPerSecond);
}

/* ----------------------------- Full Transaction ----------------------------- */

/**
 * @class LinTransactionFixture
 * @brief Fixture wiring a LinController to a PTY pair for transaction tests.
 *
 * PTY does not implement tcsendbreak() so transactions may return ERROR_BREAK;
 * tests accept both ERROR_BREAK and SUCCESS.
 */
class LinTransactionFixture : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(pty_.open(), uart::Status::SUCCESS);

    adapter_ = std::make_unique<uart::UartAdapter>(pty_.slavePath());
    uart::UartConfig cfg;
    cfg.exclusiveAccess = false;
    ASSERT_EQ(adapter_->configure(cfg), uart::Status::SUCCESS);

    controller_ = std::make_unique<lin::LinController>(*adapter_);
    lin::LinConfig linCfg;
    linCfg.baudRate = 19200;
    linCfg.responseTimeoutMs = 100;
    linCfg.enableCollisionDetection = false;
    ASSERT_EQ(controller_->configure(linCfg), lin::Status::SUCCESS);

    drainRunning_.store(true, std::memory_order_relaxed);
    drainThread_ = std::thread([this] {
      std::array<std::uint8_t, 1024> buf{};
      while (drainRunning_.load(std::memory_order_relaxed)) {
        std::size_t bytesRead = 0;
        (void)pty_.readMaster(buf.data(), buf.size(), bytesRead, 10);
      }
    });
  }

  void TearDown() override {
    drainRunning_.store(false, std::memory_order_relaxed);
    if (drainThread_.joinable()) {
      drainThread_.join();
    }

    controller_.reset();
    if (adapter_) {
      (void)adapter_->close();
      adapter_.reset();
    }
    (void)pty_.close();
  }

  const ub::PerfConfig& getCfg() { return ub::detail::getPerfConfig(); }

  uart::PtyPair pty_;
  std::unique_ptr<uart::UartAdapter> adapter_;
  std::unique_ptr<lin::LinController> controller_;
  std::atomic<bool> drainRunning_{false};
  std::thread drainThread_;
};

/**
 * @brief LIN header transmission latency via PTY.
 */
TEST_F(LinTransactionFixture, SendHeaderLatency) {
  ub::PerfConfig cfg = getCfg();
  std::string testName = "LinTransactionFixture.SendHeaderLatency";
  ub::PerfCase perf{testName, cfg};
  ub::attachProfilerHooks(perf, cfg);

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      volatile auto s = controller_->sendHeader(0x10);
      (void)s;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        auto s = controller_->sendHeader(0x10);
        ASSERT_TRUE(s == lin::Status::SUCCESS || s == lin::Status::ERROR_BREAK);
      },
      "send-header");

  std::printf("\nSend Header: %.3f us (%.0f ops/s)\n", R.stats.median, R.callsPerSecond);
}

/**
 * @brief LIN full frame transmission latency via PTY.
 */
TEST_F(LinTransactionFixture, SendFrameLatency) {
  ub::PerfConfig cfg = getCfg();
  std::string testName = "LinTransactionFixture.SendFrameLatency";
  ub::PerfCase perf{testName, cfg};
  ub::attachProfilerHooks(perf, cfg);

  const std::array<std::uint8_t, 4> DATA = {0xDE, 0xAD, 0xBE, 0xEF};

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      volatile auto s = controller_->sendFrame(0x10, DATA.data(), DATA.size());
      (void)s;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        auto s = controller_->sendFrame(0x10, DATA.data(), DATA.size());
        ASSERT_TRUE(s == lin::Status::SUCCESS || s == lin::Status::ERROR_BREAK);
      },
      "send-frame-4b");

  std::printf("\nSend Frame (4B): %.3f us (%.0f ops/s)\n", R.stats.median, R.callsPerSecond);
}

/**
 * @brief Raw UART write baseline at LIN frame size for protocol-overhead comparison.
 */
TEST_F(LinTransactionFixture, ProtocolOverhead) {
  ub::PerfConfig cfg = getCfg();
  std::string testName = "LinTransactionFixture.ProtocolOverhead";
  ub::PerfCase perf{testName, cfg};
  ub::attachProfilerHooks(perf, cfg);

  std::array<std::uint8_t, 7> rawFrame = {0x55, 0xD0, 0xDE, 0xAD, 0xBE, 0xEF, 0x00};

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      std::size_t written = 0;
      volatile auto s = adapter_->write(rawFrame.data(), rawFrame.size(), written, 100);
      (void)s;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        auto s = adapter_->write(rawFrame.data(), rawFrame.size(), written, 100);
        ASSERT_EQ(s, uart::Status::SUCCESS);
      },
      "raw-uart-write-7b");

  std::printf("\nRaw UART write (7B): %.3f us (%.0f ops/s)\n", R.stats.median, R.callsPerSecond);
}

PERF_MAIN()
