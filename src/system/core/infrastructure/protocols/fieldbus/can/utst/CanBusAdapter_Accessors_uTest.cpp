/**
 * @file CanBusAdapter_Accessors_uTest.cpp
 * @brief Minimal coverage for inline accessors on CANBusAdapter (no socket ops).
 */

#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/CanBusAdapter.hpp"

#include <gtest/gtest.h>

using apex::protocols::fieldbus::can::CANBusAdapter;

/** @test Touch inline getters to keep header coverage healthy. */
TEST(CanBusAdapterAccessors, InlineAccessors) {
  CANBusAdapter adapter("Header CAN", "dummy0");

  // Verify accessors return the constructor-provided values.
  EXPECT_EQ(adapter.description(), std::string("Header CAN"));
  EXPECT_EQ(adapter.channel(), std::string("dummy0"));
}

/** @test enableLogging() toggles without side effects (header inline coverage). */
TEST(CanBusAdapterAccessors, EnableLoggingToggleNoCrash) {
  CANBusAdapter adapter("Header CAN", "dummy0");
  adapter.enableLogging(true);
  adapter.enableLogging(false);
  SUCCEED(); // no crash / no throw behavior
}
