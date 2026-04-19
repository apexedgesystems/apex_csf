/**
 * @file NgspiceWrapper_uTest.cpp
 * @brief Unit tests for NgspiceWrapper.
 */

#include "src/sim/electronics/algorithms/spice/ngspice/inc/NgspiceWrapper.hpp"

#include <gtest/gtest.h>

using sim::electronics::spice::ngspice::NgspiceStatus;
using sim::electronics::spice::ngspice::NgspiceWrapper;

/* ----------------------- Default Construction --------------------------- */

/** @test */
TEST(NgspiceWrapper, DefaultConstruction) {
  NgspiceWrapper wrapper;
  // Should construct without error
  EXPECT_TRUE(true);
}

/* --------------------------- Enum Tests --------------------------------- */

/** @test */
TEST(NgspiceStatus, ToString) {
  EXPECT_STREQ(toString(NgspiceStatus::OK), "OK");
  EXPECT_STREQ(toString(NgspiceStatus::ERROR_NOT_INITIALIZED), "ERROR_NOT_INITIALIZED");
  EXPECT_STREQ(toString(NgspiceStatus::ERROR_NETLIST_LOAD_FAILED), "ERROR_NETLIST_LOAD_FAILED");
  EXPECT_STREQ(toString(NgspiceStatus::ERROR_SIMULATION_FAILED), "ERROR_SIMULATION_FAILED");
  EXPECT_STREQ(toString(NgspiceStatus::ERROR_NODE_NOT_FOUND), "ERROR_NODE_NOT_FOUND");
  EXPECT_STREQ(toString(NgspiceStatus::ERROR_LIBNGSPICE_NOT_AVAILABLE),
               "ERROR_LIBNGSPICE_NOT_AVAILABLE");
}

/** @test Unknown status returns UNKNOWN string. */
TEST(NgspiceStatus, ToStringUnknown) {
  auto UNKNOWN_STATUS = static_cast<NgspiceStatus>(255);
  EXPECT_STREQ(toString(UNKNOWN_STATUS), "UNKNOWN");
}

/* -------------------------- Library Availability ------------------------ */

/** @test */
TEST(NgspiceWrapper, LibraryAvailability) {
  bool available = NgspiceWrapper::isLibngspiceAvailable();

#if APEX_HAS_LIBNGSPICE
  EXPECT_TRUE(available);
#else
  EXPECT_FALSE(available);
#endif
}

/** @test */
TEST(NgspiceWrapper, VersionString) {
  NgspiceWrapper wrapper;
  std::string version = wrapper.getVersion();

  // Should return some version string (even if stub)
  EXPECT_FALSE(version.empty());
}

/* ------------------------- Netlist Loading ------------------------------ */

/** @test */
TEST(NgspiceWrapper, LoadNonexistentNetlist) {
  NgspiceWrapper wrapper;
  auto status = wrapper.loadNetlist("/nonexistent/path/circuit.sp");

  // Should fail (either because file doesn't exist or libngspice not available)
  EXPECT_NE(status, NgspiceStatus::OK);
}

/** @test */
TEST(NgspiceWrapper, LoadNetlistFromEmptyString) {
  NgspiceWrapper wrapper;
  auto status = wrapper.loadNetlistFromString("");

#if APEX_HAS_LIBNGSPICE
  // With libngspice, empty netlist might succeed (netlist parsing is ngspice's
  // responsibility)
  EXPECT_TRUE(status == NgspiceStatus::OK || status == NgspiceStatus::ERROR_NETLIST_LOAD_FAILED);
#else
  EXPECT_EQ(status, NgspiceStatus::ERROR_LIBNGSPICE_NOT_AVAILABLE);
#endif
}

/* ----------------------- Simulation (Stub) ------------------------------ */

/** @test */
TEST(NgspiceWrapper, DcOperatingPointWithoutNetlist) {
  NgspiceWrapper wrapper;
  auto status = wrapper.runDcOperatingPoint();

#if APEX_HAS_LIBNGSPICE
  // Should fail because no netlist loaded
  EXPECT_NE(status, NgspiceStatus::OK);
#else
  EXPECT_EQ(status, NgspiceStatus::ERROR_LIBNGSPICE_NOT_AVAILABLE);
#endif
}

/** @test */
TEST(NgspiceWrapper, TransientWithoutNetlist) {
  NgspiceWrapper wrapper;
  auto status = wrapper.runTransient(1e-6, 1e-9);

#if APEX_HAS_LIBNGSPICE
  // Should fail because no netlist loaded
  EXPECT_NE(status, NgspiceStatus::OK);
#else
  EXPECT_EQ(status, NgspiceStatus::ERROR_LIBNGSPICE_NOT_AVAILABLE);
#endif
}

/* ----------------------- Result Extraction ------------------------------ */

/** @test */
TEST(NgspiceWrapper, GetNonexistentNodeVoltage) {
  NgspiceWrapper wrapper;
  double voltage = 0.0;
  auto status = wrapper.getNodeVoltage("NONEXISTENT", voltage);

  EXPECT_EQ(status, NgspiceStatus::ERROR_NODE_NOT_FOUND);
}

/** @test */
TEST(NgspiceWrapper, GetAllNodeVoltagesEmpty) {
  NgspiceWrapper wrapper;
  auto& voltages = wrapper.getAllNodeVoltages();

  // Should be empty before any simulation
  EXPECT_TRUE(voltages.empty());
}

/** @test */
TEST(NgspiceWrapper, GetNonexistentWaveform) {
  NgspiceWrapper wrapper;
  std::vector<double> times, voltages;
  auto status = wrapper.getNodeWaveform("NONEXISTENT", times, voltages);

  EXPECT_EQ(status, NgspiceStatus::ERROR_NODE_NOT_FOUND);
}

/* ---------------------------- Clear ------------------------------------ */

/** @test */
TEST(NgspiceWrapper, Clear) {
  NgspiceWrapper wrapper;
  wrapper.clear(); // Should not crash

  // After clear, node voltages should be empty
  EXPECT_TRUE(wrapper.getAllNodeVoltages().empty());
}
