/**
 * @file VCanInterface_Accessors_uTest.cpp
 * @brief Minimal coverage for inline accessors on VCanInterface (no system calls).
 */

#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/VCanInterface.hpp"

#include <gtest/gtest.h>

using apex::protocols::fieldbus::can::util::VCanInterface;

/** @test Touch inline getters to keep header coverage healthy. */
TEST(VCanInterfaceAccessors, InlineAccessors) {
  VCanInterface vcan("vcanX", /*autoTeardown=*/false, /*useSudo=*/true);

  EXPECT_EQ(vcan.interfaceName(), std::string("vcanX"));
  EXPECT_TRUE(vcan.useSudo());
}
