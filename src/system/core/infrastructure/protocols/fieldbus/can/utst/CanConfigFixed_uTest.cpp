/**
 * @file CanConfigFixed_uTest.cpp
 * @test CanConfigFixed template for heap-free configuration.
 */

#include "CanAdapter_TestSupport_uTest.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/CanBusAdapter.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/VCanInterface.hpp"

#include <gtest/gtest.h>

using apex::protocols::fieldbus::can::CANBusAdapter;
using apex::protocols::fieldbus::can::CanConfig;
using apex::protocols::fieldbus::can::CanConfigFixed;
using apex::protocols::fieldbus::can::CanFilter;
using apex::protocols::fieldbus::can::CanFrame;
using apex::protocols::fieldbus::can::Status;
using apex::protocols::fieldbus::can::util::VCanInterface;
using test_support::testIfName;

/* ----------------------------- Default Construction ----------------------------- */

/** @test CanConfigFixed default construction initializes correctly. */
TEST(CanConfigFixedStruct, DefaultConstruction) {
  CanConfigFixed<4> cfg;
  EXPECT_EQ(cfg.bitrate, 500000u);
  EXPECT_FALSE(cfg.listenOnly);
  EXPECT_FALSE(cfg.loopback);
  EXPECT_EQ(cfg.filterCount, 0u);
}

/* ----------------------------- Filter Management ----------------------------- */

/** @test addFilter() adds filter and increments count. */
TEST(CanConfigFixedStruct, AddFilterSuccess) {
  CanConfigFixed<4> cfg;

  CanFilter f1{.id = 0x123, .mask = 0x7FF, .extended = false};
  EXPECT_TRUE(cfg.addFilter(f1));
  EXPECT_EQ(cfg.filterCount, 1u);
  EXPECT_EQ(cfg.filters[0].id, 0x123u);

  CanFilter f2{.id = 0x456, .mask = 0x7FF, .extended = false};
  EXPECT_TRUE(cfg.addFilter(f2));
  EXPECT_EQ(cfg.filterCount, 2u);
}

/** @test addFilter() returns false when array is full. */
TEST(CanConfigFixedStruct, AddFilterFull) {
  CanConfigFixed<2> cfg;

  EXPECT_TRUE(cfg.addFilter(CanFilter{.id = 0x001, .mask = 0x7FF, .extended = false}));
  EXPECT_TRUE(cfg.addFilter(CanFilter{.id = 0x002, .mask = 0x7FF, .extended = false}));
  EXPECT_FALSE(cfg.addFilter(CanFilter{.id = 0x003, .mask = 0x7FF, .extended = false}));
  EXPECT_EQ(cfg.filterCount, 2u);
}

/** @test clearFilters() resets filter count. */
TEST(CanConfigFixedStruct, ClearFilters) {
  CanConfigFixed<4> cfg;
  cfg.addFilter(CanFilter{.id = 0x100, .mask = 0x7FF, .extended = false});
  cfg.addFilter(CanFilter{.id = 0x200, .mask = 0x7FF, .extended = false});
  EXPECT_EQ(cfg.filterCount, 2u);

  cfg.clearFilters();
  EXPECT_EQ(cfg.filterCount, 0u);
}

/* ----------------------------- Conversion ----------------------------- */

/** @test toCanConfig() produces equivalent CanConfig. */
TEST(CanConfigFixedStruct, ToCanConfig) {
  CanConfigFixed<4> fixed;
  fixed.bitrate = 250000;
  fixed.listenOnly = true;
  fixed.loopback = true;
  fixed.addFilter(CanFilter{.id = 0x111, .mask = 0x7FF, .extended = false});
  fixed.addFilter(CanFilter{.id = 0x222, .mask = 0x7FF, .extended = true});

  CanConfig cfg = fixed.toCanConfig();

  EXPECT_EQ(cfg.bitrate, 250000u);
  EXPECT_TRUE(cfg.listenOnly);
  EXPECT_TRUE(cfg.loopback);
  EXPECT_EQ(cfg.filters.size(), 2u);
  EXPECT_EQ(cfg.filters[0].id, 0x111u);
  EXPECT_EQ(cfg.filters[1].id, 0x222u);
}

/** @test toCanConfig() with no filters produces empty vector. */
TEST(CanConfigFixedStruct, ToCanConfigNoFilters) {
  CanConfigFixed<4> fixed;
  fixed.loopback = true;

  CanConfig cfg = fixed.toCanConfig();

  EXPECT_TRUE(cfg.loopback);
  EXPECT_TRUE(cfg.filters.empty());
}

/* ----------------------------- Adapter Integration ----------------------------- */

/** @test CANBusAdapter::configure() accepts CanConfigFixed. */
TEST(CanConfigFixedAdapter, ConfigureWithFixedConfig) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Fixed Config Test", vcan.interfaceName());

  CanConfigFixed<4> cfg;
  cfg.loopback = true;
  cfg.addFilter(CanFilter{.id = 0x123, .mask = 0x7FF, .extended = false});

  EXPECT_EQ(adapter.configure(cfg), Status::SUCCESS);
}

/** @test CanConfigFixed zero-size works for no filters. */
TEST(CanConfigFixedStruct, ZeroSizeConfig) {
  CanConfigFixed<0> cfg;
  cfg.loopback = true;

  // Cannot add filters (size is 0)
  // addFilter would fail at compile time or return false

  CanConfig converted = cfg.toCanConfig();
  EXPECT_TRUE(converted.loopback);
  EXPECT_TRUE(converted.filters.empty());
}
