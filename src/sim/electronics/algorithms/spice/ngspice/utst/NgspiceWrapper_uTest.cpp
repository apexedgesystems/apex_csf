/**
 * @file NgspiceWrapper_uTest.cpp
 * @brief Unit tests for NgspiceWrapper.
 */

#include "src/sim/electronics/algorithms/spice/ngspice/inc/NgspiceWrapper.hpp"

#include <gtest/gtest.h>

using sim::electronics::algorithms::spice::ngspice::NgspiceStatus;
using sim::electronics::algorithms::spice::ngspice::NgspiceWrapper;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default-constructed wrapper is valid */
TEST(NgspiceWrapperTest, DefaultConstruction) {
  NgspiceWrapper wrapper;
  EXPECT_TRUE(wrapper.getAllNodeVoltages().empty());
}

/* ----------------------------- Enum Tests ----------------------------- */

/** @test toString covers every NgspiceStatus enumerator */
TEST(NgspiceStatusTest, ToString) {
  EXPECT_STREQ(toString(NgspiceStatus::OK), "OK");
  EXPECT_STREQ(toString(NgspiceStatus::ERROR_NOT_INITIALIZED), "ERROR_NOT_INITIALIZED");
  EXPECT_STREQ(toString(NgspiceStatus::ERROR_NETLIST_LOAD_FAILED), "ERROR_NETLIST_LOAD_FAILED");
  EXPECT_STREQ(toString(NgspiceStatus::ERROR_SIMULATION_FAILED), "ERROR_SIMULATION_FAILED");
  EXPECT_STREQ(toString(NgspiceStatus::ERROR_NODE_NOT_FOUND), "ERROR_NODE_NOT_FOUND");
  EXPECT_STREQ(toString(NgspiceStatus::ERROR_LIBNGSPICE_NOT_AVAILABLE),
               "ERROR_LIBNGSPICE_NOT_AVAILABLE");
}

/** @test Unknown status returns UNKNOWN string. */
TEST(NgspiceStatusTest, ToStringUnknown) {
  auto UNKNOWN_STATUS = static_cast<NgspiceStatus>(255);
  EXPECT_STREQ(toString(UNKNOWN_STATUS), "UNKNOWN");
}

/* ----------------------------- Library Availability ----------------------------- */

/** @test isLibngspiceAvailable matches the APEX_HAS_LIBNGSPICE build define */
TEST(NgspiceWrapperTest, LibraryAvailability) {
  bool available = NgspiceWrapper::isLibngspiceAvailable();

#if APEX_HAS_LIBNGSPICE
  EXPECT_TRUE(available);
#else
  EXPECT_FALSE(available);
#endif
}

/** @test getVersion returns a non-empty string regardless of libngspice availability */
TEST(NgspiceWrapperTest, VersionString) {
  NgspiceWrapper wrapper;
  const std::string VERSION = wrapper.getVersion();
  EXPECT_FALSE(VERSION.empty());
}

/* ----------------------------- Netlist Loading ----------------------------- */

/** @test loadNetlist fails for a missing file */
TEST(NgspiceWrapperTest, LoadNonexistentNetlist) {
  NgspiceWrapper wrapper;
  auto status = wrapper.loadNetlist("/nonexistent/path/circuit.sp");
  EXPECT_NE(status, NgspiceStatus::OK);
}

/** @test loadNetlistFromString delegates parsing to libngspice (or reports unavailable) */
TEST(NgspiceWrapperTest, LoadNetlistFromEmptyString) {
  NgspiceWrapper wrapper;
  auto status = wrapper.loadNetlistFromString("");

#if APEX_HAS_LIBNGSPICE
  EXPECT_TRUE(status == NgspiceStatus::OK || status == NgspiceStatus::ERROR_NETLIST_LOAD_FAILED);
#else
  EXPECT_EQ(status, NgspiceStatus::ERROR_LIBNGSPICE_NOT_AVAILABLE);
#endif
}

/* ----------------------------- Simulation ----------------------------- */

/** @test runDcOperatingPoint without a loaded netlist returns a non-OK status */
TEST(NgspiceWrapperTest, DcOperatingPointWithoutNetlist) {
  NgspiceWrapper wrapper;
  auto status = wrapper.runDcOperatingPoint();

#if APEX_HAS_LIBNGSPICE
  EXPECT_NE(status, NgspiceStatus::OK);
#else
  EXPECT_EQ(status, NgspiceStatus::ERROR_LIBNGSPICE_NOT_AVAILABLE);
#endif
}

/** @test runTransient without a loaded netlist returns a non-OK status */
TEST(NgspiceWrapperTest, TransientWithoutNetlist) {
  NgspiceWrapper wrapper;
  auto status = wrapper.runTransient(1e-6, 1e-9);

#if APEX_HAS_LIBNGSPICE
  EXPECT_NE(status, NgspiceStatus::OK);
#else
  EXPECT_EQ(status, NgspiceStatus::ERROR_LIBNGSPICE_NOT_AVAILABLE);
#endif
}

/* ----------------------------- Result Extraction ----------------------------- */

/** @test getNodeVoltage returns ERROR_NODE_NOT_FOUND for an unknown node */
TEST(NgspiceWrapperTest, GetNonexistentNodeVoltage) {
  NgspiceWrapper wrapper;
  double voltage = 0.0;
  auto status = wrapper.getNodeVoltage("NONEXISTENT", voltage);
  EXPECT_EQ(status, NgspiceStatus::ERROR_NODE_NOT_FOUND);
}

/** @test getAllNodeVoltages is empty before any simulation has run */
TEST(NgspiceWrapperTest, GetAllNodeVoltagesEmpty) {
  NgspiceWrapper wrapper;
  EXPECT_TRUE(wrapper.getAllNodeVoltages().empty());
}

/** @test getNodeWaveform returns ERROR_NODE_NOT_FOUND for an unknown node */
TEST(NgspiceWrapperTest, GetNonexistentWaveform) {
  NgspiceWrapper wrapper;
  std::vector<double> times, voltages;
  auto status = wrapper.getNodeWaveform("NONEXISTENT", times, voltages);
  EXPECT_EQ(status, NgspiceStatus::ERROR_NODE_NOT_FOUND);
}

/* ----------------------------- Clear ----------------------------- */

/** @test clear() resets node voltages to empty */
TEST(NgspiceWrapperTest, Clear) {
  NgspiceWrapper wrapper;
  wrapper.clear();
  EXPECT_TRUE(wrapper.getAllNodeVoltages().empty());
}
