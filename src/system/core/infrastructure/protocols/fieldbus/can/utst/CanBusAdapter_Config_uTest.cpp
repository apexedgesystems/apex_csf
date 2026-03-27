/**
 * @file CanBusAdapter_Config_uTest.cpp
 * @test Configuration semantics and guards (no external socket required here).
 */

#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/CanBusAdapter.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/VCanInterface.hpp"

#include <gtest/gtest.h>

using apex::protocols::fieldbus::can::CANBusAdapter;
using apex::protocols::fieldbus::can::CanConfig;
using apex::protocols::fieldbus::can::CanFilter;
using apex::protocols::fieldbus::can::CanFrame;
using apex::protocols::fieldbus::can::Status;
using apex::protocols::fieldbus::can::util::VCanInterface;

/** @test send/recv before configure() return ERROR_NOT_CONFIGURED. */
TEST(CanBusAdapterConfig, NotConfiguredGuards) {
  VCanInterface vcan("vcan0", /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Config CAN", vcan.interfaceName());

  CanFrame f{};
  EXPECT_EQ(adapter.send(f, 0), Status::ERROR_NOT_CONFIGURED);
  EXPECT_EQ(adapter.recv(f, 0), Status::ERROR_NOT_CONFIGURED);
}

/** @test Basic configure() succeeds and repeat configure() is OK (socket already open). */
TEST(CanBusAdapterConfig, ConfigureIdempotent) {
  VCanInterface vcan("vcan0", /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Config CAN", vcan.interfaceName());

  CanConfig cfg;
  cfg.loopback = true; // empty filters → accept-all fast path
  EXPECT_EQ(adapter.configure(cfg), Status::SUCCESS);
  // Second call should early-return from openAndBindIfNeeded() and reapply opts cleanly.
  EXPECT_EQ(adapter.configure(cfg), Status::SUCCESS);
}

/** @test Reconfigure with loopback toggled (exercise setLoopback_ twice). */
TEST(CanBusAdapterConfig, ReconfigureLoopbackToggle) {
  VCanInterface vcan("vcan0", /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Config CAN", vcan.interfaceName());

  CanConfig cfg{};
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  cfg.loopback = false; // flip loopback and reapply on already-open socket
  EXPECT_EQ(adapter.configure(cfg), Status::SUCCESS);
}

/**
 * @test Non-empty filters apply successfully.
 * Covers applyFilters_ path including both standard and extended filter encoding.
 */
TEST(CanBusAdapterConfig, ConfigureWithStdAndExtFilters) {
  VCanInterface vcan("vcan0", /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Config CAN", vcan.interfaceName());

  CanConfig cfg{};
  cfg.loopback = true;
  // One standard 11-bit filter, one extended 29-bit filter.
  cfg.filters = {CanFilter{.id = 0x123, .mask = 0x7FF, .extended = false},
                 CanFilter{.id = 0x1ABCDE, .mask = 0x1FFFFFFF, .extended = true}};

  EXPECT_EQ(adapter.configure(cfg), Status::SUCCESS);
}

/** @test Accessors return initialization values (header inline coverage). */
TEST(CanBusAdapterConfig, AccessorsDescriptionAndChannel) {
  VCanInterface vcan("vcan0", /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  const std::string DESC = "Config CAN";
  const std::string IFNAME = vcan.interfaceName();
  CANBusAdapter adapter(DESC, IFNAME);

  EXPECT_EQ(adapter.description(), DESC);
  EXPECT_EQ(adapter.channel(), IFNAME);
}
