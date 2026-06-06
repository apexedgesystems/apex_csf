/**
 * @file CanBusAdapter_pTest.cpp
 * @brief Performance tests for the CAN bus adapter send/recv paths.
 *
 * Measures:
 *  - Loopback round-trip latency at 1B and 8B payloads
 *  - Send throughput (fire-and-forget)
 *  - Individual recv vs recvBatch syscall reduction
 *  - Event loop poll overhead with and without data
 *  - CanConfig vs CanConfigFixed configuration latency
 *  - Statistics tracking overhead
 *  - Payload size scaling (0-8 bytes)
 *
 * Usage:
 *   ./CanBusAdapter_PTEST --csv results.csv
 *   ./CanBusAdapter_PTEST --quick
 *   ./CanBusAdapter_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/CanBusAdapter.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/CanEventLoop.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/VCanInterface.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace ub = vernier::bench;
using namespace std::chrono_literals;

using apex::protocols::fieldbus::can::CANBusAdapter;
using apex::protocols::fieldbus::can::CanConfig;
using apex::protocols::fieldbus::can::CanConfigFixed;
using apex::protocols::fieldbus::can::CanEventCallback;
using apex::protocols::fieldbus::can::CanEventLoop;
using apex::protocols::fieldbus::can::CanFrame;
using apex::protocols::fieldbus::can::CanId;
using apex::protocols::fieldbus::can::Status;
using apex::protocols::fieldbus::can::util::VCanInterface;

/* ----------------------------- Test Fixtures ----------------------------- */

namespace {

/** @brief Generate a unique vcan interface name per test invocation. */
std::string uniqueVcanName(const char*) {
  static std::atomic_int counter{0};
  return std::string("vpt") + std::to_string(counter.fetch_add(1));
}

/**
 * @brief RAII wrapper for a vcan interface with two adapters for loopback.
 *
 * CAN loopback delivers frames to OTHER sockets on the same interface, so a
 * dedicated sender and receiver are used.
 */
class VCanFixture {
public:
  explicit VCanFixture(const std::string& ifName)
      : vcan_(ifName, true, true), sender_("PerfSender", ifName),
        receiver_("PerfReceiver", ifName) {}

  bool setup() {
    if (!vcan_.setup())
      return false;

    CanConfig cfg{};
    cfg.loopback = true;

    if (sender_.configure(cfg) != Status::SUCCESS)
      return false;
    if (receiver_.configure(cfg) != Status::SUCCESS)
      return false;

    return true;
  }

  CANBusAdapter& sender() { return sender_; }
  CANBusAdapter& receiver() { return receiver_; }
  VCanInterface& vcan() { return vcan_; }

private:
  VCanInterface vcan_;
  CANBusAdapter sender_;
  CANBusAdapter receiver_;
};

/** @brief Build a CAN frame with the specified DLC and byte pattern. */
CanFrame makeFrame(std::uint32_t id, std::uint8_t dlc, std::uint8_t pattern = 0xAA) {
  CanFrame f{};
  f.canId = CanId{.id = id, .extended = false, .remote = false, .error = false};
  f.dlc = dlc;
  const std::uint8_t LEN = (dlc <= 8) ? dlc : 8;
  for (std::uint8_t i = 0; i < LEN; ++i) {
    f.data[i] = static_cast<std::uint8_t>(pattern + i);
  }
  return f;
}

} // namespace

/* ----------------------------- Loopback Latency ----------------------------- */

/**
 * @brief CAN loopback round-trip latency with 1-byte payload.
 */
PERF_TEST(CanBusPerf, LoopbackLatency1B) {
  UB_PERF_GUARD(perf);

  VCanFixture fixture(uniqueVcanName("LoopLat1"));
  ASSERT_TRUE(fixture.setup()) << "Failed to setup vcan";

  CanFrame tx = makeFrame(0x100, 1);
  CanFrame rx{};

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)fixture.sender().send(tx, 100);
      (void)fixture.receiver().recv(rx, 100);
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        Status st = fixture.sender().send(tx, 100);
        ASSERT_EQ(st, Status::SUCCESS) << "send failed";
        st = fixture.receiver().recv(rx, 100);
        ASSERT_EQ(st, Status::SUCCESS) << "recv failed";
      },
      "loopback_1B");

  std::printf("\n[LoopbackLatency1B]\n");
  std::printf("  Round-trip: %.3f us (median), %.3f us (p10), %.3f us (p90)\n", result.stats.median,
              result.stats.p10, result.stats.p90);
  std::printf("  Throughput: %.0f frames/sec\n", result.callsPerSecond);
  std::printf("  Jitter (CV%%): %.1f%%\n", result.stats.cv * 100);
}

/**
 * @brief CAN loopback round-trip latency with full 8-byte payload.
 */
PERF_TEST(CanBusPerf, LoopbackLatency8B) {
  UB_PERF_GUARD(perf);

  VCanFixture fixture(uniqueVcanName("LoopLat8"));
  ASSERT_TRUE(fixture.setup()) << "Failed to setup vcan";

  CanFrame tx = makeFrame(0x200, 8);
  CanFrame rx{};

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)fixture.sender().send(tx, 100);
      (void)fixture.receiver().recv(rx, 100);
    }
  });

  ub::MemoryProfile memProfile{
      .bytesRead = 8,
      .bytesWritten = 8,
      .bytesAllocated = 0,
  };

  auto result = perf.throughputLoop(
      [&] {
        Status st = fixture.sender().send(tx, 100);
        ASSERT_EQ(st, Status::SUCCESS);
        st = fixture.receiver().recv(rx, 100);
        ASSERT_EQ(st, Status::SUCCESS);
      },
      "loopback_8B", memProfile);

  std::printf("\n[LoopbackLatency8B]\n");
  std::printf("  Round-trip: %.3f us (median), %.3f us (p10), %.3f us (p90)\n", result.stats.median,
              result.stats.p10, result.stats.p90);
  std::printf("  Throughput: %.0f frames/sec\n", result.callsPerSecond);
  std::printf("  Jitter (CV%%): %.1f%%\n", result.stats.cv * 100);
}

/* ----------------------------- Send Throughput ----------------------------- */

/**
 * @brief Maximum send throughput (fire-and-forget).
 */
PERF_TEST(CanBusPerf, SendThroughput) {
  UB_PERF_GUARD(perf);

  VCanFixture fixture(uniqueVcanName("SendThru"));
  ASSERT_TRUE(fixture.setup()) << "Failed to setup vcan";

  CanFrame tx = makeFrame(0x300, 8);

  CanFrame drain{};
  while (fixture.receiver().recv(drain, 0) == Status::SUCCESS) {
  }

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)fixture.sender().send(tx, 0);
    }
    while (fixture.receiver().recv(drain, 10) == Status::SUCCESS) {
    }
  });

  volatile Status sink = Status::SUCCESS;
  auto result = perf.throughputLoop([&] { sink = fixture.sender().send(tx, 100); }, "send_only");

  (void)sink;
  std::printf("\n[SendThroughput]\n");
  std::printf("  Send latency: %.3f us (median)\n", result.stats.median);
  std::printf("  Throughput: %.0f frames/sec\n", result.callsPerSecond);
}

/* ----------------------------- Batch Receive ----------------------------- */

/**
 * @brief Compare individual recv() vs recvBatch() draining throughput.
 */
PERF_TEST(CanBusPerf, BatchRecvComparison) {
  UB_PERF_GUARD(perf);

  VCanFixture fixture(uniqueVcanName("BatchRecv"));
  ASSERT_TRUE(fixture.setup()) << "Failed to setup vcan";

  constexpr std::size_t FRAMES_PER_BATCH = 8;
  std::array<CanFrame, FRAMES_PER_BATCH> rxBuffer{};

  auto preSendFrames = [&]() {
    for (std::size_t i = 0; i < FRAMES_PER_BATCH; ++i) {
      CanFrame tx = makeFrame(static_cast<std::uint32_t>(0x400 + i), 8);
      Status st = fixture.sender().send(tx, 100);
      if (st != Status::SUCCESS)
        return false;
    }
    std::this_thread::sleep_for(1ms);
    return true;
  };

  {
    ASSERT_TRUE(preSendFrames());

    perf.warmup([&] {
      for (int iter = 0; iter < perf.cycles(); ++iter) {
        for (std::size_t i = 0; i < FRAMES_PER_BATCH; ++i) {
          (void)fixture.receiver().recv(rxBuffer[i], 100);
        }
        preSendFrames();
      }
    });

    ASSERT_TRUE(preSendFrames());
    auto result = perf.throughputLoop(
        [&] {
          for (std::size_t i = 0; i < FRAMES_PER_BATCH; ++i) {
            Status st = fixture.receiver().recv(rxBuffer[i], 100);
            if (st != Status::SUCCESS)
              break;
          }
        },
        "recv_individual");

    std::printf("\n[BatchRecvComparison - Individual recv()]\n");
    std::printf("  Time to drain %zu frames: %.3f us\n", FRAMES_PER_BATCH, result.stats.median);
    std::printf("  Throughput: %.0f batches/sec\n", result.callsPerSecond);
  }

  while (fixture.receiver().recv(rxBuffer[0], 10) == Status::SUCCESS) {
  }

  {
    ASSERT_TRUE(preSendFrames());

    perf.warmup([&] {
      for (int iter = 0; iter < perf.cycles(); ++iter) {
        (void)fixture.receiver().recvBatch(rxBuffer.data(), FRAMES_PER_BATCH, 100);
        preSendFrames();
      }
    });

    ASSERT_TRUE(preSendFrames());
    auto result = perf.throughputLoop(
        [&] {
          std::size_t count = fixture.receiver().recvBatch(rxBuffer.data(), FRAMES_PER_BATCH, 100);
          (void)count;
        },
        "recv_batch");

    std::printf("\n[BatchRecvComparison - recvBatch()]\n");
    std::printf("  Time to drain %zu frames: %.3f us\n", FRAMES_PER_BATCH, result.stats.median);
    std::printf("  Throughput: %.0f batches/sec\n", result.callsPerSecond);
  }
}

/* ----------------------------- Event Loop ----------------------------- */

/** @brief Callback context for the event loop benchmark. */
struct EventLoopContext {
  CANBusAdapter* adapter;
  std::atomic<std::size_t> framesReceived;
};

/** @brief Event loop callback that drains the adapter into the context counter. */
void eventLoopCallback(void* ctx, CANBusAdapter* adapter, std::uint32_t) noexcept {
  auto* ectx = static_cast<EventLoopContext*>(ctx);
  CanFrame frame{};
  while (adapter->recv(frame, 0) == Status::SUCCESS) {
    ectx->framesReceived.fetch_add(1, std::memory_order_relaxed);
  }
}

/**
 * @brief Event loop poll overhead with and without data available.
 */
PERF_TEST(CanBusPerf, EventLoopOverhead) {
  UB_PERF_GUARD(perf);

  VCanFixture fixture(uniqueVcanName("EventLoop"));
  ASSERT_TRUE(fixture.setup()) << "Failed to setup vcan";

  CanEventLoop loop;
  ASSERT_TRUE(loop.init()) << "Failed to init event loop";

  EventLoopContext ctx{&fixture.receiver(), {0}};
  CanEventCallback callback{eventLoopCallback, &ctx};
  ASSERT_TRUE(loop.add(&fixture.receiver(), callback)) << "Failed to add adapter";

  {
    auto result = perf.throughputLoop([&] { (void)loop.poll(0); }, "poll_empty");

    std::printf("\n[EventLoopOverhead - poll() with no data]\n");
    std::printf("  Poll latency: %.3f us (median)\n", result.stats.median);
    std::printf("  Throughput: %.0f polls/sec\n", result.callsPerSecond);
  }

  {
    CanFrame tx = makeFrame(0x500, 4);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        (void)fixture.sender().send(tx, 100);
        (void)loop.poll(10);
      }
    });

    ctx.framesReceived.store(0, std::memory_order_relaxed);
    auto result = perf.throughputLoop(
        [&] {
          Status st = fixture.sender().send(tx, 100);
          ASSERT_EQ(st, Status::SUCCESS);
          int events = loop.poll(10);
          ASSERT_GE(events, 0);
        },
        "poll_with_data");

    std::printf("\n[EventLoopOverhead - poll() with data]\n");
    std::printf("  Send+poll latency: %.3f us (median)\n", result.stats.median);
    std::printf("  Throughput: %.0f cycles/sec\n", result.callsPerSecond);
    std::printf("  Frames received via callback: %zu\n",
                ctx.framesReceived.load(std::memory_order_relaxed));
  }

  ASSERT_TRUE(loop.remove(&fixture.receiver()));
}

/* ----------------------------- Configuration ----------------------------- */

/**
 * @brief Compare CanConfig (heap) vs CanConfigFixed (stack) configure() latency.
 */
PERF_TEST(CanBusPerf, ConfigurationComparison) {
  UB_PERF_GUARD(perf);

  VCanInterface vcan(uniqueVcanName("ConfigCmp"), true, true);
  ASSERT_TRUE(vcan.setup()) << "Failed to setup vcan";

  {
    CANBusAdapter adapter("ConfigTest", vcan.interfaceName());
    CanConfig cfg{};
    cfg.loopback = true;

    auto result = perf.throughputLoop(
        [&] {
          Status st = adapter.configure(cfg);
          ASSERT_EQ(st, Status::SUCCESS);
        },
        "config_heap");

    std::printf("\n[ConfigurationComparison - CanConfig (heap)]\n");
    std::printf("  Configure latency: %.3f us (median)\n", result.stats.median);
    std::printf("  Throughput: %.0f configs/sec\n", result.callsPerSecond);
  }

  {
    CANBusAdapter adapter("ConfigTest", vcan.interfaceName());
    CanConfigFixed<4> cfg{};
    cfg.loopback = true;

    auto result = perf.throughputLoop(
        [&] {
          Status st = adapter.configure(cfg);
          ASSERT_EQ(st, Status::SUCCESS);
        },
        "config_fixed");

    std::printf("\n[ConfigurationComparison - CanConfigFixed (stack)]\n");
    std::printf("  Configure latency: %.3f us (median)\n", result.stats.median);
    std::printf("  Throughput: %.0f configs/sec\n", result.callsPerSecond);
  }
}

/* ----------------------------- Statistics Overhead ----------------------------- */

/**
 * @brief Statistics tracking overhead on the send/recv hot path.
 */
PERF_TEST(CanBusPerf, StatsOverhead) {
  UB_PERF_GUARD(perf);

  VCanFixture fixture(uniqueVcanName("StatsOH"));
  ASSERT_TRUE(fixture.setup()) << "Failed to setup vcan";

  CanFrame tx = makeFrame(0x600, 8);
  CanFrame rx{};

  fixture.sender().resetStats();
  fixture.receiver().resetStats();

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)fixture.sender().send(tx, 100);
      (void)fixture.receiver().recv(rx, 100);
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        Status st = fixture.sender().send(tx, 100);
        ASSERT_EQ(st, Status::SUCCESS);
        st = fixture.receiver().recv(rx, 100);
        ASSERT_EQ(st, Status::SUCCESS);
      },
      "with_stats");

  auto senderStats = fixture.sender().stats();
  auto recvStats = fixture.receiver().stats();

  std::printf("\n[StatsOverhead]\n");
  std::printf("  Round-trip: %.3f us (median)\n", result.stats.median);
  std::printf("  Throughput: %.0f frames/sec\n", result.callsPerSecond);
  std::printf("  Sender stats: sent=%lu bytes=%lu\n", senderStats.framesSent,
              senderStats.bytesTransmitted);
  std::printf("  Receiver stats: recv=%lu bytes=%lu\n", recvStats.framesReceived,
              recvStats.bytesReceived);
}

/* ----------------------------- Payload Scaling ----------------------------- */

/**
 * @brief Latency across CAN payload sizes (0-8 bytes).
 */
PERF_TEST(CanBusPerf, PayloadScaling) {
  UB_PERF_GUARD(perf);

  VCanFixture fixture(uniqueVcanName("PayloadScale"));
  ASSERT_TRUE(fixture.setup()) << "Failed to setup vcan";

  struct TestCase {
    const char* name;
    std::uint8_t dlc;
  };

  std::vector<TestCase> tests = {{"0B", 0}, {"1B", 1}, {"2B", 2}, {"4B", 4}, {"6B", 6}, {"8B", 8}};

  std::printf("\n%-8s %-12s %-15s\n", "DLC", "Latency(us)", "Throughput");
  std::printf("%s\n", std::string(40, '-').c_str());

  CanFrame rx{};

  for (const auto& test : tests) {
    CanFrame tx = makeFrame(0x700, test.dlc);

    for (int i = 0; i < 100; ++i) {
      (void)fixture.sender().send(tx, 100);
      (void)fixture.receiver().recv(rx, 100);
    }

    ub::PerfConfig cfg = ub::detail::getPerfConfig();
    cfg.msgBytes = test.dlc;
    std::string testName = "CanBusPerf.PayloadScaling/" + std::string(test.name);
    ub::PerfCase subPerf{testName, cfg};

    auto result = subPerf.throughputLoop(
        [&] {
          (void)fixture.sender().send(tx, 100);
          (void)fixture.receiver().recv(rx, 100);
        },
        test.name);

    std::printf("%-8s %-12.1f %-15.0f\n", test.name, result.stats.median, result.callsPerSecond);
  }
}

PERF_MAIN()
