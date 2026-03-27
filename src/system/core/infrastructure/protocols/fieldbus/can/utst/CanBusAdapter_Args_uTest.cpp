/**
 * @file CanBusAdapter_Args_uTest.cpp
 * @test Argument validation and defensive checks.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/CanBusAdapter.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/VCanInterface.hpp"

#include <gtest/gtest.h>

using apex::protocols::fieldbus::can::CANBusAdapter;
using apex::protocols::fieldbus::can::CanConfig;
using apex::protocols::fieldbus::can::CanFrame;
using apex::protocols::fieldbus::can::CanId;
using apex::protocols::fieldbus::can::Status;
using apex::protocols::fieldbus::can::util::VCanInterface;

/**
 * @test send() with dlc > 8 returns ERROR_INVALID_ARG.
 * Parameterized to cover several out-of-range values without duplication elsewhere.
 */
class CanBusAdapterArgsInvalidDlc : public ::testing::TestWithParam<uint8_t> {};

INSTANTIATE_TEST_SUITE_P(InvalidDlcValues, CanBusAdapterArgsInvalidDlc,
                         ::testing::Values(static_cast<uint8_t>(9), static_cast<uint8_t>(10),
                                           static_cast<uint8_t>(15), static_cast<uint8_t>(255)));

TEST_P(CanBusAdapterArgsInvalidDlc, SendRejectsInvalidDlc) {
  VCanInterface vcan("vcan0", /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Args CAN", vcan.interfaceName());

  CanConfig cfg;
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  CanFrame f{};
  f.canId = CanId{.id = 0x1, .extended = false, .remote = false, .error = false};
  f.dlc = GetParam();

  EXPECT_EQ(adapter.send(f, 0), Status::ERROR_INVALID_ARG);
}

/**
 * @test configure() fails with an unknown/nonexistent interface name.
 * Exercises open/bind failure path (SIOCGIFINDEX / bind) → ERROR_IO.
 * Note: no vcan setup here by design.
 */
TEST(CanBusAdapterArgs, ConfigureFailsOnUnknownInterface) {
  static const char* kBogusIf = "this_if_does_not_exist_12345";
  CANBusAdapter adapter("Args CAN", kBogusIf);

  CanConfig cfg{};
  cfg.loopback = true; // value irrelevant; open/bind should fail first
  EXPECT_EQ(adapter.configure(cfg), Status::ERROR_IO);
}
