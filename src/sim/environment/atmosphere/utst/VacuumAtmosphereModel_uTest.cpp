/**
 * @file VacuumAtmosphereModel_uTest.cpp
 * @brief Tests for the Moon vacuum wrapper.
 *
 * The wrapper is a ConstantAtmosphere locked to (rho = T = P = 0). A query
 * returns the WARN_VACUUM_QUERY status with a zero-filled state so a drag
 * consumer can short-circuit.
 */

#include "src/sim/environment/atmosphere/inc/AtmosphereModelBase.hpp"
#include "src/sim/environment/atmosphere/inc/moon/VacuumAtmosphereModel.hpp"

#include <gtest/gtest.h>

#include <cmath>

using sim::environment::atmosphere::AtmosphereState;
using sim::environment::atmosphere::Status;
using sim::environment::atmosphere::moon::VacuumAtmosphereModel;

namespace {

TEST(VacuumAtmosphereModel, IsVacuum) {
  VacuumAtmosphereModel a;
  EXPECT_TRUE(a.isVacuum());
  EXPECT_DOUBLE_EQ(a.rho(), 0.0);
  EXPECT_DOUBLE_EQ(a.T(), 0.0);
  EXPECT_DOUBLE_EQ(a.P(), 0.0);
}

TEST(VacuumAtmosphereModel, QueryWarnsAndReturnsZeroState) {
  VacuumAtmosphereModel a;
  AtmosphereState s;
  // Vacuum query at any altitude: warning status, zero state.
  EXPECT_EQ(a.query(0.0, 0.0, 0.0, s), Status::WARN_VACUUM_QUERY);
  EXPECT_DOUBLE_EQ(s.rho, 0.0);
  EXPECT_DOUBLE_EQ(s.P, 0.0);
  EXPECT_DOUBLE_EQ(s.T, 0.0);
  EXPECT_DOUBLE_EQ(s.a, 0.0);

  AtmosphereState s_high;
  EXPECT_EQ(a.query(1.0e6, 0.0, 0.0, s_high), Status::WARN_VACUUM_QUERY);
  EXPECT_DOUBLE_EQ(s_high.rho, 0.0);
}

TEST(VacuumAtmosphereModel, ValidAtAnyAltitude) {
  VacuumAtmosphereModel a;
  EXPECT_TRUE(std::isinf(a.minAltitudeM()));
  EXPECT_TRUE(std::isinf(a.maxAltitudeM()));
  EXPECT_TRUE(a.isInValidRange(-1e6));
  EXPECT_TRUE(a.isInValidRange(1e9));
}

} // namespace
