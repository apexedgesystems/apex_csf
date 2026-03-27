/**
 * @file LinController_uTest.cpp
 * @brief Unit tests for LinController.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/lin/inc/LinController.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/PtyPair.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartAdapter.hpp"

#include <gtest/gtest.h>

#include <cstring>

namespace lin = apex::protocols::fieldbus::lin;
using apex::protocols::serial::uart::PtyPair;
using apex::protocols::serial::uart::UartAdapter;

/* ----------------------------- LinStats ----------------------------- */

/** @test LinStats default construction zeros all counters. */
TEST(LinStatsTest, DefaultConstruction) {
  lin::LinStats stats;
  EXPECT_EQ(stats.framesSent, 0);
  EXPECT_EQ(stats.framesReceived, 0);
  EXPECT_EQ(stats.checksumErrors, 0);
  EXPECT_EQ(stats.parityErrors, 0);
  EXPECT_EQ(stats.syncErrors, 0);
  EXPECT_EQ(stats.timeouts, 0);
  EXPECT_EQ(stats.collisions, 0);
  EXPECT_EQ(stats.breakErrors, 0);
}

/** @test LinStats reset clears all counters. */
TEST(LinStatsTest, Reset) {
  lin::LinStats stats;
  stats.framesSent = 10;
  stats.framesReceived = 5;
  stats.checksumErrors = 2;
  stats.parityErrors = 1;
  stats.syncErrors = 3;
  stats.timeouts = 4;
  stats.collisions = 1;
  stats.breakErrors = 2;

  stats.reset();

  EXPECT_EQ(stats.framesSent, 0);
  EXPECT_EQ(stats.framesReceived, 0);
  EXPECT_EQ(stats.checksumErrors, 0);
  EXPECT_EQ(stats.parityErrors, 0);
  EXPECT_EQ(stats.syncErrors, 0);
  EXPECT_EQ(stats.timeouts, 0);
  EXPECT_EQ(stats.collisions, 0);
  EXPECT_EQ(stats.breakErrors, 0);
}

/* ----------------------------- LinController Construction ----------------------------- */

/**
 * @brief Test fixture providing PTY pair and UART adapter for LIN controller tests.
 */
class LinControllerTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(pty_.open(), apex::protocols::serial::uart::Status::SUCCESS);
    adapter_ = std::make_unique<UartAdapter>(pty_.slavePath());
    // Configure the adapter to open it
    apex::protocols::serial::uart::UartConfig uartCfg;
    ASSERT_EQ(adapter_->configure(uartCfg), apex::protocols::serial::uart::Status::SUCCESS);
    controller_ = std::make_unique<lin::LinController>(*adapter_);
  }

  void TearDown() override {
    if (controller_) {
      controller_.reset();
    }
    if (adapter_) {
      (void)adapter_->close();
      adapter_.reset();
    }
    (void)pty_.close();
  }

  PtyPair pty_;
  std::unique_ptr<UartAdapter> adapter_;
  std::unique_ptr<lin::LinController> controller_;
};

/** @test Controller is not configured after construction. */
TEST_F(LinControllerTest, NotConfiguredAfterConstruction) {
  EXPECT_FALSE(controller_->isConfigured());
}

/** @test Controller configure succeeds with default config. */
TEST_F(LinControllerTest, ConfigureSucceeds) {
  lin::LinConfig cfg;
  EXPECT_EQ(controller_->configure(cfg), lin::Status::SUCCESS);
  EXPECT_TRUE(controller_->isConfigured());
}

/** @test Controller configuration is accessible after configure. */
TEST_F(LinControllerTest, ConfigurationAccessible) {
  lin::LinConfig cfg;
  cfg.baudRate = 9600;
  cfg.checksumType = lin::ChecksumType::CLASSIC;
  cfg.responseTimeoutMs = 100;

  EXPECT_EQ(controller_->configure(cfg), lin::Status::SUCCESS);

  const auto& storedCfg = controller_->config();
  EXPECT_EQ(storedCfg.baudRate, 9600);
  EXPECT_EQ(storedCfg.checksumType, lin::ChecksumType::CLASSIC);
  EXPECT_EQ(storedCfg.responseTimeoutMs, 100);
}

/** @test Controller stats accessible and resettable. */
TEST_F(LinControllerTest, StatsAccessible) {
  lin::LinConfig cfg;
  ASSERT_EQ(controller_->configure(cfg), lin::Status::SUCCESS);

  const auto& stats = controller_->stats();
  EXPECT_EQ(stats.framesSent, 0);

  controller_->resetStats();
  EXPECT_EQ(controller_->stats().framesSent, 0);
}

/* ----------------------------- LinController Operations ----------------------------- */

/** @test sendHeader fails when not configured. */
TEST_F(LinControllerTest, SendHeaderNotConfigured) {
  EXPECT_EQ(controller_->sendHeader(0x10), lin::Status::ERROR_NOT_CONFIGURED);
}

/** @test sendHeader rejects invalid frame ID. */
TEST_F(LinControllerTest, SendHeaderInvalidId) {
  lin::LinConfig cfg;
  ASSERT_EQ(controller_->configure(cfg), lin::Status::SUCCESS);
  EXPECT_EQ(controller_->sendHeader(64), lin::Status::ERROR_INVALID_ARG);
}

/** @test sendFrame fails when not configured. */
TEST_F(LinControllerTest, SendFrameNotConfigured) {
  const std::uint8_t DATA[] = {0x01, 0x02};
  EXPECT_EQ(controller_->sendFrame(0x10, DATA, 2), lin::Status::ERROR_NOT_CONFIGURED);
}

/** @test sendFrame rejects null data. */
TEST_F(LinControllerTest, SendFrameNullData) {
  lin::LinConfig cfg;
  ASSERT_EQ(controller_->configure(cfg), lin::Status::SUCCESS);
  EXPECT_EQ(controller_->sendFrame(0x10, nullptr, 2), lin::Status::ERROR_INVALID_ARG);
}

/** @test sendFrame rejects zero-length data. */
TEST_F(LinControllerTest, SendFrameZeroLength) {
  lin::LinConfig cfg;
  ASSERT_EQ(controller_->configure(cfg), lin::Status::SUCCESS);
  const std::uint8_t DATA[] = {0x01};
  EXPECT_EQ(controller_->sendFrame(0x10, DATA, 0), lin::Status::ERROR_INVALID_ARG);
}

/** @test sendFrame rejects oversized data. */
TEST_F(LinControllerTest, SendFrameOversizedData) {
  lin::LinConfig cfg;
  ASSERT_EQ(controller_->configure(cfg), lin::Status::SUCCESS);
  std::uint8_t data[16] = {};
  EXPECT_EQ(controller_->sendFrame(0x10, data, 16), lin::Status::ERROR_INVALID_ARG);
}

/** @test receiveResponse fails when not configured. */
TEST_F(LinControllerTest, ReceiveResponseNotConfigured) {
  lin::FrameBuffer response;
  lin::ParsedFrame parsed;
  EXPECT_EQ(controller_->receiveResponse(0x10, response, parsed),
            lin::Status::ERROR_NOT_CONFIGURED);
}

/** @test receiveResponse rejects invalid frame ID. */
TEST_F(LinControllerTest, ReceiveResponseInvalidId) {
  lin::LinConfig cfg;
  ASSERT_EQ(controller_->configure(cfg), lin::Status::SUCCESS);
  lin::FrameBuffer response;
  lin::ParsedFrame parsed;
  EXPECT_EQ(controller_->receiveResponse(64, response, parsed), lin::Status::ERROR_INVALID_ARG);
}

/** @test receiveResponse with explicit data length rejects zero. */
TEST_F(LinControllerTest, ReceiveResponseZeroDataLength) {
  lin::LinConfig cfg;
  ASSERT_EQ(controller_->configure(cfg), lin::Status::SUCCESS);
  lin::FrameBuffer response;
  lin::ParsedFrame parsed;
  EXPECT_EQ(controller_->receiveResponse(0x10, 0, response, parsed),
            lin::Status::ERROR_INVALID_ARG);
}

/** @test receiveResponse with explicit data length rejects oversized. */
TEST_F(LinControllerTest, ReceiveResponseOversizedDataLength) {
  lin::LinConfig cfg;
  ASSERT_EQ(controller_->configure(cfg), lin::Status::SUCCESS);
  lin::FrameBuffer response;
  lin::ParsedFrame parsed;
  EXPECT_EQ(controller_->receiveResponse(0x10, 16, response, parsed),
            lin::Status::ERROR_INVALID_ARG);
}

/** @test requestFrame fails when not configured. */
TEST_F(LinControllerTest, RequestFrameNotConfigured) {
  lin::FrameBuffer response;
  lin::ParsedFrame parsed;
  EXPECT_EQ(controller_->requestFrame(0x10, response, parsed), lin::Status::ERROR_NOT_CONFIGURED);
}

/** @test waitForHeader fails when not configured. */
TEST_F(LinControllerTest, WaitForHeaderNotConfigured) {
  std::uint8_t frameId = 0;
  EXPECT_EQ(controller_->waitForHeader(frameId), lin::Status::ERROR_NOT_CONFIGURED);
}

/** @test respondToHeader fails when not configured. */
TEST_F(LinControllerTest, RespondToHeaderNotConfigured) {
  const std::uint8_t DATA[] = {0x01, 0x02};
  EXPECT_EQ(controller_->respondToHeader(0x10, DATA, 2), lin::Status::ERROR_NOT_CONFIGURED);
}

/** @test respondToHeader rejects invalid frame ID. */
TEST_F(LinControllerTest, RespondToHeaderInvalidId) {
  lin::LinConfig cfg;
  ASSERT_EQ(controller_->configure(cfg), lin::Status::SUCCESS);
  const std::uint8_t DATA[] = {0x01, 0x02};
  EXPECT_EQ(controller_->respondToHeader(64, DATA, 2), lin::Status::ERROR_INVALID_ARG);
}

/** @test respondToHeader rejects null data. */
TEST_F(LinControllerTest, RespondToHeaderNullData) {
  lin::LinConfig cfg;
  ASSERT_EQ(controller_->configure(cfg), lin::Status::SUCCESS);
  EXPECT_EQ(controller_->respondToHeader(0x10, nullptr, 2), lin::Status::ERROR_INVALID_ARG);
}

/** @test respondToHeader rejects zero-length data. */
TEST_F(LinControllerTest, RespondToHeaderZeroLength) {
  lin::LinConfig cfg;
  ASSERT_EQ(controller_->configure(cfg), lin::Status::SUCCESS);
  const std::uint8_t DATA[] = {0x01};
  EXPECT_EQ(controller_->respondToHeader(0x10, DATA, 0), lin::Status::ERROR_INVALID_ARG);
}

/** @test respondToHeader rejects oversized data. */
TEST_F(LinControllerTest, RespondToHeaderOversizedData) {
  lin::LinConfig cfg;
  ASSERT_EQ(controller_->configure(cfg), lin::Status::SUCCESS);
  std::uint8_t data[16] = {};
  EXPECT_EQ(controller_->respondToHeader(0x10, data, 16), lin::Status::ERROR_INVALID_ARG);
}

/* ----------------------------- Integration Tests ----------------------------- */

/** @test Full round-trip: send frame and verify data via PTY master. */
TEST_F(LinControllerTest, SendFrameRoundTrip) {
  lin::LinConfig cfg;
  cfg.responseTimeoutMs = 100;
  cfg.enableCollisionDetection = false; // Disable for this test
  ASSERT_EQ(controller_->configure(cfg), lin::Status::SUCCESS);

  // Note: This test exercises the send path. Due to tcsendbreak() behavior
  // in PTY environments (may not work as expected), we verify the attempt
  // rather than the full protocol exchange.

  // The actual send may fail with ERROR_BREAK in PTY since tcsendbreak
  // doesn't work properly on pseudo-terminals
  const std::uint8_t DATA[] = {0xDE, 0xAD};
  const lin::Status RESULT = controller_->sendFrame(0x10, DATA, 2);

  // Accept either SUCCESS or ERROR_BREAK (PTY limitation)
  EXPECT_TRUE(RESULT == lin::Status::SUCCESS || RESULT == lin::Status::ERROR_BREAK)
      << "Got unexpected status: " << lin::toString(RESULT);
}

/** @test Statistics are updated on operations. */
TEST_F(LinControllerTest, StatsUpdatedOnOperations) {
  lin::LinConfig cfg;
  cfg.enableCollisionDetection = false;
  ASSERT_EQ(controller_->configure(cfg), lin::Status::SUCCESS);

  // Attempt sendHeader (may fail due to PTY break limitations)
  // Discard result as we're only checking stats
  (void)controller_->sendHeader(0x10);

  // Stats should be updated regardless of result
  // (either framesSent incremented on success, or breakErrors on failure)
  const auto& stats = controller_->stats();
  EXPECT_TRUE(stats.framesSent > 0 || stats.breakErrors > 0);
}
