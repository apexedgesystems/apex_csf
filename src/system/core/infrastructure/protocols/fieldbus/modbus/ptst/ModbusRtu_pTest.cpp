/**
 * @file ModbusRtu_pTest.cpp
 * @brief Performance tests for Modbus RTU frame and transaction paths.
 *
 * Measures:
 *  - Frame build for read/write coils and single/multiple registers
 *  - CRC-16/MODBUS throughput on small frames, max frames, and 1KB buffers
 *  - Response parsing throughput
 *  - PTY-loopback transaction latency for read/write holding registers
 *  - Latency distribution and protocol overhead vs raw UART
 *
 * Usage:
 *   ./ModbusRtu_PTEST --csv results.csv
 *   ./ModbusRtu_PTEST --quick
 *   ./ModbusRtu_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusFrame.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusMaster.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusRtuTransport.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/PtyPair.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartAdapter.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace ub = vernier::bench;
namespace modbus = apex::protocols::fieldbus::modbus;
namespace uart = apex::protocols::serial::uart;

/* ----------------------------- Local Helpers ----------------------------- */

namespace {

inline const ub::PerfConfig& getCfg() { return ub::detail::getPerfConfig(); }

inline std::vector<std::uint16_t> makeRegisters(std::size_t count) {
  std::vector<std::uint16_t> regs(count);
  for (std::size_t i = 0; i < count; ++i) {
    regs[i] = static_cast<std::uint16_t>(0x1000 + i);
  }
  return regs;
}

} // namespace

/* ----------------------------- Frame Building ----------------------------- */

/**
 * @brief Build a read-holding-registers request (FrameBuffer + CRC).
 */
PERF_TEST(ModbusFrameBuild, ReadHoldingRegisters) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  modbus::FrameBuffer frame;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      frame.reset();
      volatile auto s = modbus::buildReadHoldingRegistersRequest(frame, 1, 0, 10);
      (void)s;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        frame.reset();
        auto s = modbus::buildReadHoldingRegistersRequest(frame, 1, 0, 10);
        ASSERT_EQ(s, modbus::Status::SUCCESS);
      },
      "build-read-holding");

  std::printf("\nBuild ReadHoldingRegisters: %.3f us (%.0f frames/s)\n", R.stats.median,
              R.callsPerSecond);
}

/**
 * @brief Build a write-multiple-registers request with 10 registers.
 */
PERF_TEST(ModbusFrameBuild, WriteMultipleRegisters) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  const std::size_t REG_COUNT = 10;
  const auto regs = makeRegisters(REG_COUNT);
  modbus::FrameBuffer frame;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      frame.reset();
      volatile auto s =
          modbus::buildWriteMultipleRegistersRequest(frame, 1, 0, regs.data(), REG_COUNT);
      (void)s;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        frame.reset();
        auto s = modbus::buildWriteMultipleRegistersRequest(frame, 1, 0, regs.data(), REG_COUNT);
        ASSERT_EQ(s, modbus::Status::SUCCESS);
      },
      "build-write-multi-regs");

  std::printf("\nBuild WriteMultipleRegisters (%zu regs): %.3f us (%.0f frames/s)\n", REG_COUNT,
              R.stats.median, R.callsPerSecond);
}

/**
 * @brief Build a read-coils request.
 */
PERF_TEST(ModbusFrameBuild, ReadCoils) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  modbus::FrameBuffer frame;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      frame.reset();
      volatile auto s = modbus::buildReadCoilsRequest(frame, 1, 0, 16);
      (void)s;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        frame.reset();
        auto s = modbus::buildReadCoilsRequest(frame, 1, 0, 16);
        ASSERT_EQ(s, modbus::Status::SUCCESS);
      },
      "build-read-coils");

  std::printf("\nBuild ReadCoils: %.3f us (%.0f frames/s)\n", R.stats.median, R.callsPerSecond);
}

/**
 * @brief Build a write-single-register request (minimal frame).
 */
PERF_TEST(ModbusFrameBuild, WriteSingleRegister) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  modbus::FrameBuffer frame;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      frame.reset();
      volatile auto s = modbus::buildWriteSingleRegisterRequest(frame, 1, 100, 0x1234);
      (void)s;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        frame.reset();
        auto s = modbus::buildWriteSingleRegisterRequest(frame, 1, 100, 0x1234);
        ASSERT_EQ(s, modbus::Status::SUCCESS);
      },
      "build-write-single-reg");

  std::printf("\nBuild WriteSingleRegister: %.3f us (%.0f frames/s)\n", R.stats.median,
              R.callsPerSecond);
}

/* ----------------------------- CRC Calculation ----------------------------- */

/**
 * @brief CRC-16/MODBUS on a small (6B) read-request frame.
 */
PERF_TEST(ModbusCrc, SmallFrame) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  std::array<std::uint8_t, 6> frame = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto crc = modbus::calculateCrc(frame.data(), frame.size());
      (void)crc;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        volatile auto crc = modbus::calculateCrc(frame.data(), frame.size());
        (void)crc;
      },
      "crc-small");

  std::printf("\nCRC small frame (%zuB): %.3f us (%.0f calcs/s)\n", frame.size(), R.stats.median,
              R.callsPerSecond);
}

/**
 * @brief CRC-16/MODBUS on a maximum-size 254B frame.
 */
PERF_TEST(ModbusCrc, MaxFrame) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  std::array<std::uint8_t, 254> frame{};
  for (std::size_t i = 0; i < frame.size(); ++i) {
    frame[i] = static_cast<std::uint8_t>(i);
  }

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto crc = modbus::calculateCrc(frame.data(), frame.size());
      (void)crc;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        volatile auto crc = modbus::calculateCrc(frame.data(), frame.size());
        (void)crc;
      },
      "crc-max");

  const double MBps = (R.callsPerSecond * frame.size()) / (1024.0 * 1024.0);
  std::printf("\nCRC max frame (%zuB): %.3f us (%.0f calcs/s, %.1f MB/s)\n", frame.size(),
              R.stats.median, R.callsPerSecond, MBps);
}

/**
 * @brief CRC-16/MODBUS throughput on a 1KB buffer.
 */
PERF_TEST(ModbusCrc, Throughput) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  std::array<std::uint8_t, 1024> buffer{};
  for (std::size_t i = 0; i < buffer.size(); ++i) {
    buffer[i] = static_cast<std::uint8_t>(i);
  }

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto crc = modbus::calculateCrc(buffer.data(), buffer.size());
      (void)crc;
    }
  });

  ub::MemoryProfile memProfile{.bytesRead = buffer.size(), .bytesWritten = 0, .bytesAllocated = 0};

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        volatile auto crc = modbus::calculateCrc(buffer.data(), buffer.size());
        (void)crc;
      },
      "crc-throughput", memProfile);

  const double MBps = (R.callsPerSecond * buffer.size()) / (1024.0 * 1024.0);
  std::printf("\nCRC throughput: %.1f MB/s\n", MBps);
}

/* ----------------------------- Response Parsing ----------------------------- */

/**
 * @brief Extract registers from a read-holding-registers response.
 */
PERF_TEST(ModbusParse, ReadHoldingRegistersResponse) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  std::array<std::uint8_t, 25> response = {0x01, 0x03, 0x14, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03,
                                           0x00, 0x04, 0x00, 0x05, 0x00, 0x06, 0x00, 0x07, 0x00,
                                           0x08, 0x00, 0x09, 0x00, 0x0A, 0x00, 0x00};

  const std::uint16_t CRC = modbus::calculateCrc(response.data(), response.size() - 2);
  response[23] = static_cast<std::uint8_t>(CRC & 0xFF);
  response[24] = static_cast<std::uint8_t>(CRC >> 8);

  std::array<std::uint16_t, 10> values{};

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto count =
          modbus::extractRegistersFromResponse(response.data(), response.size(), values.data(), 10);
      (void)count;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        auto count = modbus::extractRegistersFromResponse(response.data(), response.size(),
                                                          values.data(), 10);
        ASSERT_EQ(count, 10u);
      },
      "parse-read-holding");

  std::printf("\nParse ReadHoldingRegisters (10 regs): %.3f us (%.0f parses/s)\n", R.stats.median,
              R.callsPerSecond);
}

/* ----------------------------- Full Transaction ----------------------------- */

/**
 * @class ModbusTransactionFixture
 * @brief Fixture wiring a ModbusMaster to a PTY pair with an in-thread responder.
 */
class ModbusTransactionFixture : public ::testing::Test {
protected:
  static constexpr std::uint8_t UNIT_ADDRESS = 1;

  void SetUp() override {
    ASSERT_EQ(pty_.open(), uart::Status::SUCCESS);

    adapter_ = std::make_unique<uart::UartAdapter>(pty_.slavePath());
    uart::UartConfig cfg;
    cfg.exclusiveAccess = false;
    ASSERT_EQ(adapter_->configure(cfg), uart::Status::SUCCESS);

    modbus::ModbusRtuConfig rtuConfig;
    rtuConfig.responseTimeoutMs = 100;
    rtuConfig.interFrameDelayUs = 0;
    transport_ = std::make_unique<modbus::ModbusRtuTransport>(adapter_.get(), rtuConfig, 115200);
    ASSERT_EQ(transport_->open(), modbus::Status::SUCCESS);

    modbus::MasterConfig masterConfig;
    master_ = std::make_unique<modbus::ModbusMaster>(transport_.get(), masterConfig);

    responderRunning_ = true;
    responderThread_ = std::thread(&ModbusTransactionFixture::responderLoop, this);
  }

  void TearDown() override {
    responderRunning_ = false;
    if (responderThread_.joinable()) {
      responderThread_.join();
    }

    master_.reset();
    if (transport_) {
      (void)transport_->close();
    }
    if (adapter_) {
      (void)adapter_->close();
    }
  }

  void responderLoop() {
    std::array<std::uint8_t, 256> rxBuffer{};
    std::array<std::uint8_t, 256> txBuffer{};

    while (responderRunning_) {
      std::size_t bytesRead = 0;
      auto status = pty_.readMaster(rxBuffer.data(), rxBuffer.size(), bytesRead, 10);

      if (status == uart::Status::SUCCESS && bytesRead >= 4) {
        const std::uint16_t RECEIVED_CRC =
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(rxBuffer[bytesRead - 2]) |
                                       (static_cast<std::uint16_t>(rxBuffer[bytesRead - 1]) << 8));
        const std::uint16_t CALC_CRC = modbus::calculateCrc(rxBuffer.data(), bytesRead - 2);

        if (RECEIVED_CRC == CALC_CRC && rxBuffer[0] == UNIT_ADDRESS) {
          std::size_t respLen = buildResponse(rxBuffer.data(), bytesRead, txBuffer.data());
          if (respLen > 0) {
            std::size_t bytesWritten = 0;
            (void)pty_.writeMaster(txBuffer.data(), respLen, bytesWritten, 50);
          }
        }
      }
    }
  }

  std::size_t buildResponse(const std::uint8_t* request, std::size_t reqLen,
                            std::uint8_t* response) {
    const std::uint8_t FC = request[1];

    if (FC == 0x03 && reqLen >= 8) {
      const std::uint16_t QUANTITY =
          static_cast<std::uint16_t>((static_cast<std::uint16_t>(request[4]) << 8) | request[5]);
      const std::size_t BYTE_COUNT = static_cast<const std::size_t>(QUANTITY * 2);

      response[0] = UNIT_ADDRESS;
      response[1] = FC;
      response[2] = static_cast<std::uint8_t>(BYTE_COUNT);

      for (std::size_t i = 0; i < BYTE_COUNT; ++i) {
        response[3 + i] = static_cast<std::uint8_t>(i);
      }

      const std::uint16_t CRC = modbus::calculateCrc(response, 3 + BYTE_COUNT);
      response[3 + BYTE_COUNT] = static_cast<std::uint8_t>(CRC & 0xFF);
      response[3 + BYTE_COUNT + 1] = static_cast<std::uint8_t>(CRC >> 8);

      return 3 + BYTE_COUNT + 2;
    }

    if (FC == 0x06 && reqLen >= 8) {
      std::memcpy(response, request, 6);
      const std::uint16_t CRC = modbus::calculateCrc(response, 6);
      response[6] = static_cast<std::uint8_t>(CRC & 0xFF);
      response[7] = static_cast<std::uint8_t>(CRC >> 8);
      return 8;
    }

    return 0;
  }

  const ub::PerfConfig& getCfg() { return ub::detail::getPerfConfig(); }

  uart::PtyPair pty_;
  std::unique_ptr<uart::UartAdapter> adapter_;
  std::unique_ptr<modbus::ModbusRtuTransport> transport_;
  std::unique_ptr<modbus::ModbusMaster> master_;

  std::thread responderThread_;
  std::atomic<bool> responderRunning_{false};
};

/**
 * @brief Full RTU read-holding-registers transaction latency.
 */
TEST_F(ModbusTransactionFixture, ReadHoldingRegisters) {
  ub::PerfConfig cfg = getCfg();
  std::string testName = "ModbusTransactionFixture.ReadHoldingRegisters";
  ub::PerfCase perf{testName, cfg};
  ub::attachProfilerHooks(perf, cfg);

  std::array<std::uint16_t, 10> values{};

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      auto result = master_->readHoldingRegisters(UNIT_ADDRESS, 0, 10, values.data(), 200);
      ASSERT_TRUE(result.ok()) << "Warmup failed: " << static_cast<int>(result.status);
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        auto result = master_->readHoldingRegisters(UNIT_ADDRESS, 0, 10, values.data(), 200);
        ASSERT_TRUE(result.ok()) << "Status: " << static_cast<int>(result.status);
      },
      "tx-read-holding");

  std::printf("\nReadHoldingRegisters transaction: %.3f us (%.0f tx/s)\n", R.stats.median,
              R.callsPerSecond);
}

/**
 * @brief Full RTU write-single-register transaction latency.
 */
TEST_F(ModbusTransactionFixture, WriteSingleRegister) {
  ub::PerfConfig cfg = getCfg();
  std::string testName = "ModbusTransactionFixture.WriteSingleRegister";
  ub::PerfCase perf{testName, cfg};
  ub::attachProfilerHooks(perf, cfg);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      auto result = master_->writeSingleRegister(UNIT_ADDRESS, 0, 0x1234, 200);
      ASSERT_TRUE(result.ok()) << "Warmup failed";
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        auto result = master_->writeSingleRegister(UNIT_ADDRESS, 0, 0x1234, 200);
        ASSERT_TRUE(result.ok());
      },
      "tx-write-single");

  std::printf("\nWriteSingleRegister transaction: %.3f us (%.0f tx/s)\n", R.stats.median,
              R.callsPerSecond);
}

/**
 * @brief Sustained transaction throughput over a burst of 100 reads.
 */
TEST_F(ModbusTransactionFixture, Throughput) {
  ub::PerfConfig cfg = getCfg();
  std::string testName = "ModbusTransactionFixture.Throughput";
  ub::PerfCase perf{testName, cfg};
  ub::attachProfilerHooks(perf, cfg);

  std::array<std::uint16_t, 10> values{};

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  for (int i = 0; i < 50; ++i) {
    (void)master_->readHoldingRegisters(UNIT_ADDRESS, 0, 10, values.data(), 200);
  }

  constexpr int BURST_SIZE = 100;
  const auto START = std::chrono::steady_clock::now();

  for (int i = 0; i < BURST_SIZE; ++i) {
    auto result = master_->readHoldingRegisters(UNIT_ADDRESS, 0, 10, values.data(), 200);
    ASSERT_TRUE(result.ok()) << "Burst transaction failed";
  }

  const auto END = std::chrono::steady_clock::now();
  const double ELAPSED_MS = std::chrono::duration<double, std::milli>(END - START).count();
  const double TXS_PER_SEC = (BURST_SIZE * 1000.0) / ELAPSED_MS;

  std::printf("\nSustained throughput: %.0f transactions/s (%.1f ms for %d tx)\n", TXS_PER_SEC,
              ELAPSED_MS, BURST_SIZE);
}

/**
 * @brief Latency distribution analysis (p10 / p50 / p90, tail ratio).
 */
TEST_F(ModbusTransactionFixture, LatencyDistribution) {
  ub::PerfConfig cfg = getCfg();
  std::string testName = "ModbusTransactionFixture.LatencyDistribution";
  ub::PerfCase perf{testName, cfg};
  ub::attachProfilerHooks(perf, cfg);

  std::array<std::uint16_t, 10> values{};

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 50); ++i) {
      (void)master_->readHoldingRegisters(UNIT_ADDRESS, 0, 10, values.data(), 200);
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        auto result = master_->readHoldingRegisters(UNIT_ADDRESS, 0, 10, values.data(), 200);
        ASSERT_TRUE(result.ok());
      },
      "tx-latency-dist");

  std::printf("\nTransaction Latency Distribution:\n");
  std::printf("  p10: %.1f us (best case)\n", R.stats.p10);
  std::printf("  p50: %.1f us (median)\n", R.stats.median);
  std::printf("  p90: %.1f us (worst case)\n", R.stats.p90);
  std::printf("  p90/p50 ratio: %.2f\n", R.stats.p90 / R.stats.median);
  std::printf("  CV: %.1f%%\n", R.stats.cv * 100.0);
}

/* ----------------------------- Protocol Overhead ----------------------------- */

/**
 * @brief Raw UART round-trip baseline for protocol-overhead comparison.
 */
PERF_TEST(ModbusOverhead, VsRawUart) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  uart::PtyPair pty;
  ASSERT_EQ(pty.open(), uart::Status::SUCCESS);

  uart::UartAdapter adapter(pty.slavePath());
  uart::UartConfig cfg;
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), uart::Status::SUCCESS);

  std::array<std::uint8_t, 8> txData = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};
  std::array<std::uint8_t, 8> rxData{};

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      std::size_t written = 0;
      (void)adapter.write(txData.data(), txData.size(), written, 100);
      std::size_t masterRead = 0;
      (void)pty.readMaster(rxData.data(), rxData.size(), masterRead, 100);
      std::size_t masterWritten = 0;
      (void)pty.writeMaster(rxData.data(), masterRead, masterWritten, 100);
      std::size_t adapterRead = 0;
      (void)adapter.read(rxData.data(), rxData.size(), adapterRead, 100);
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        auto s1 = adapter.write(txData.data(), txData.size(), written, 100);
        ASSERT_EQ(s1, uart::Status::SUCCESS);

        std::size_t masterRead = 0;
        (void)pty.readMaster(rxData.data(), rxData.size(), masterRead, 100);

        std::size_t masterWritten = 0;
        (void)pty.writeMaster(rxData.data(), masterRead, masterWritten, 100);

        std::size_t adapterRead = 0;
        auto s2 = adapter.read(rxData.data(), rxData.size(), adapterRead, 100);
        ASSERT_EQ(s2, uart::Status::SUCCESS);
      },
      "raw-uart-roundtrip");

  std::printf("\nRaw UART round-trip (8B): %.3f us (%.0f ops/s)\n", R.stats.median,
              R.callsPerSecond);
}

PERF_MAIN()
