/**
 * @file AircraftCommand_uTest.cpp
 * @brief Unit tests for Aircraft::handleCommand (command surface).
 *
 * Exercises the SwModelBase command-dispatch contract that
 * Aircraft::handleCommand fulfills:
 *   - Component-specific opcodes (0x0100+) handled inline
 *   - Common SwModelBase opcodes delegated to the base class
 *   - Payload validation rejects malformed inputs
 *   - State changes persist across calls (turbulence flag)
 *   - GET_COMMAND_STATE round-trips the live state
 *
 * No registry / executive / scheduler setup needed — handleCommand is
 * a pure virtual override that operates on the component's own state.
 * Tests construct an Aircraft directly and invoke handleCommand on it.
 */

#include "demos/apex_horizon_demo/aircraft/inc/Aircraft.hpp"
#include "demos/apex_horizon_demo/aircraft/inc/AircraftCommand.hpp"

#include "src/system/core/infrastructure/system_component/base/inc/CommandResult.hpp"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

using appsim::aircraft::Aircraft;
using appsim::aircraft::AircraftCmdSetEnable;
using appsim::aircraft::AircraftCommandSnapshot;
using appsim::aircraft::AircraftOpcode;
using system_core::system_component::CommandResult;

namespace {

/// Build a single-byte payload spanning the given enable value, suitable
/// for SET_TURBULENCE_ENABLE / SET_GUST_ALLEVIATION_ENABLE opcodes.
inline apex::compat::rospan<std::uint8_t> spanFromEnable(std::uint8_t* storage,
                                                         std::uint8_t value) {
  *storage = value;
  return apex::compat::rospan<std::uint8_t>(storage, 1);
}

} // namespace

/* ----------------------------- SET_TURBULENCE_ENABLE ----------------------------- */

TEST(AircraftCommandTest, SetTurbulenceEnableOff) {
  Aircraft a;
  std::uint8_t storage = 0;
  std::vector<std::uint8_t> resp;
  const std::uint8_t rc =
      a.handleCommand(static_cast<std::uint16_t>(AircraftOpcode::SET_TURBULENCE_ENABLE),
                      spanFromEnable(&storage, 0), resp);

  EXPECT_EQ(rc, static_cast<std::uint8_t>(CommandResult::SUCCESS));
  EXPECT_TRUE(resp.empty()) << "SET_TURBULENCE_ENABLE returns no response payload";
}

TEST(AircraftCommandTest, SetTurbulenceEnableOn) {
  Aircraft a;
  std::uint8_t storage = 1;
  std::vector<std::uint8_t> resp;
  const std::uint8_t rc =
      a.handleCommand(static_cast<std::uint16_t>(AircraftOpcode::SET_TURBULENCE_ENABLE),
                      spanFromEnable(&storage, 1), resp);

  EXPECT_EQ(rc, static_cast<std::uint8_t>(CommandResult::SUCCESS));
}

TEST(AircraftCommandTest, SetTurbulenceEnableTogglesPersist) {
  Aircraft a;
  std::vector<std::uint8_t> resp;

  // Default state: ON. Toggle OFF, query, expect 0.
  std::uint8_t off = 0;
  ASSERT_EQ(a.handleCommand(static_cast<std::uint16_t>(AircraftOpcode::SET_TURBULENCE_ENABLE),
                            spanFromEnable(&off, 0), resp),
            static_cast<std::uint8_t>(CommandResult::SUCCESS));

  resp.clear();
  ASSERT_EQ(a.handleCommand(static_cast<std::uint16_t>(AircraftOpcode::GET_COMMAND_STATE),
                            apex::compat::rospan<std::uint8_t>{}, resp),
            static_cast<std::uint8_t>(CommandResult::SUCCESS));
  ASSERT_EQ(resp.size(), sizeof(AircraftCommandSnapshot));
  AircraftCommandSnapshot snap{};
  std::memcpy(&snap, resp.data(), sizeof(snap));
  EXPECT_EQ(snap.turbulence_enabled, 0u);

  // Toggle ON, query again, expect 1.
  std::uint8_t on = 1;
  resp.clear();
  ASSERT_EQ(a.handleCommand(static_cast<std::uint16_t>(AircraftOpcode::SET_TURBULENCE_ENABLE),
                            spanFromEnable(&on, 1), resp),
            static_cast<std::uint8_t>(CommandResult::SUCCESS));

  resp.clear();
  ASSERT_EQ(a.handleCommand(static_cast<std::uint16_t>(AircraftOpcode::GET_COMMAND_STATE),
                            apex::compat::rospan<std::uint8_t>{}, resp),
            static_cast<std::uint8_t>(CommandResult::SUCCESS));
  std::memcpy(&snap, resp.data(), sizeof(snap));
  EXPECT_EQ(snap.turbulence_enabled, 1u);
}

TEST(AircraftCommandTest, SetTurbulenceEnableRejectsEmptyPayload) {
  Aircraft a;
  std::vector<std::uint8_t> resp;
  const std::uint8_t rc =
      a.handleCommand(static_cast<std::uint16_t>(AircraftOpcode::SET_TURBULENCE_ENABLE),
                      apex::compat::rospan<std::uint8_t>{}, // empty payload
                      resp);

  EXPECT_EQ(rc, static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD));
}

/* ----------------------------- GET_COMMAND_STATE ----------------------------- */

TEST(AircraftCommandTest, GetCommandStateDefaultsAreSensible) {
  Aircraft a;
  std::vector<std::uint8_t> resp;
  const std::uint8_t rc =
      a.handleCommand(static_cast<std::uint16_t>(AircraftOpcode::GET_COMMAND_STATE),
                      apex::compat::rospan<std::uint8_t>{}, resp);

  ASSERT_EQ(rc, static_cast<std::uint8_t>(CommandResult::SUCCESS));
  ASSERT_EQ(resp.size(), sizeof(AircraftCommandSnapshot));

  AircraftCommandSnapshot snap{};
  std::memcpy(&snap, resp.data(), sizeof(snap));
  EXPECT_EQ(snap.turbulence_enabled, 1u)
      << "Default turbulence_enabled must be ON to preserve pre-behavior";
  EXPECT_EQ(snap.cmd_count, 0u)
      << "Fresh Aircraft has no cmds processed yet (GET_COMMAND_STATE doesn't bump)";
}

TEST(AircraftCommandTest, CmdCountAdvancesOnSetEnable) {
  Aircraft a;
  std::vector<std::uint8_t> resp;

  // Three SET_TURBULENCE_ENABLE calls → cmd_count should be 3.
  for (int k = 0; k < 3; ++k) {
    std::uint8_t v = static_cast<std::uint8_t>(k & 1);
    resp.clear();
    ASSERT_EQ(a.handleCommand(static_cast<std::uint16_t>(AircraftOpcode::SET_TURBULENCE_ENABLE),
                              spanFromEnable(&v, v), resp),
              static_cast<std::uint8_t>(CommandResult::SUCCESS));
  }

  resp.clear();
  ASSERT_EQ(a.handleCommand(static_cast<std::uint16_t>(AircraftOpcode::GET_COMMAND_STATE),
                            apex::compat::rospan<std::uint8_t>{}, resp),
            static_cast<std::uint8_t>(CommandResult::SUCCESS));

  AircraftCommandSnapshot snap{};
  std::memcpy(&snap, resp.data(), sizeof(snap));
  EXPECT_EQ(snap.cmd_count, 3u);
}

/* ----------------------------- SET_GUST_ALLEVIATION_ENABLE ----------------------------- */

TEST(AircraftCommandTest, SetGustAlleviationEnableOff) {
  Aircraft a;
  EXPECT_TRUE(a.isGustAlleviationEnabled()); // default on
  std::uint8_t v = 0;
  std::vector<std::uint8_t> resp;
  EXPECT_EQ(a.handleCommand(static_cast<std::uint16_t>(AircraftOpcode::SET_GUST_ALLEVIATION_ENABLE),
                            spanFromEnable(&v, 0), resp),
            static_cast<std::uint8_t>(CommandResult::SUCCESS));
  EXPECT_FALSE(a.isGustAlleviationEnabled());
  EXPECT_TRUE(resp.empty());
}

TEST(AircraftCommandTest, SetGustAlleviationEnableOn) {
  Aircraft a;
  std::uint8_t off = 0, on = 1;
  std::vector<std::uint8_t> resp;
  (void)a.handleCommand(static_cast<std::uint16_t>(AircraftOpcode::SET_GUST_ALLEVIATION_ENABLE),
                        spanFromEnable(&off, 0), resp);
  EXPECT_FALSE(a.isGustAlleviationEnabled());
  EXPECT_EQ(a.handleCommand(static_cast<std::uint16_t>(AircraftOpcode::SET_GUST_ALLEVIATION_ENABLE),
                            spanFromEnable(&on, 1), resp),
            static_cast<std::uint8_t>(CommandResult::SUCCESS));
  EXPECT_TRUE(a.isGustAlleviationEnabled());
}

TEST(AircraftCommandTest, SetGustAlleviationEnableRejectsEmptyPayload) {
  Aircraft a;
  std::vector<std::uint8_t> resp;
  EXPECT_EQ(a.handleCommand(static_cast<std::uint16_t>(AircraftOpcode::SET_GUST_ALLEVIATION_ENABLE),
                            apex::compat::rospan<std::uint8_t>{}, resp),
            static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD));
  EXPECT_TRUE(a.isGustAlleviationEnabled()) << "rejection must not mutate state";
}

TEST(AircraftCommandTest, GetCommandStateReflectsGustAlleviationToggle) {
  Aircraft a;
  std::uint8_t off = 0;
  std::vector<std::uint8_t> resp;
  (void)a.handleCommand(static_cast<std::uint16_t>(AircraftOpcode::SET_GUST_ALLEVIATION_ENABLE),
                        spanFromEnable(&off, 0), resp);
  resp.clear();
  EXPECT_EQ(a.handleCommand(static_cast<std::uint16_t>(AircraftOpcode::GET_COMMAND_STATE),
                            apex::compat::rospan<std::uint8_t>{}, resp),
            static_cast<std::uint8_t>(CommandResult::SUCCESS));
  ASSERT_EQ(resp.size(), sizeof(AircraftCommandSnapshot));
  AircraftCommandSnapshot snap{};
  std::memcpy(&snap, resp.data(), sizeof(snap));
  EXPECT_EQ(snap.gust_alleviation_enabled, 0u);
  EXPECT_EQ(snap.turbulence_enabled, 1u); // unchanged
}

/* ----------------------------- Unknown opcode delegation ----------------------------- */

TEST(AircraftCommandTest, UnknownOpcodeDelegatesToBaseClass) {
  // Anything outside our 0x0100+ component-specific range gets passed
  // to SwModelBase. For an opcode the base also doesn't know
  // (e.g., 0x0FFF — outside both the system and base ranges), the
  // base returns UNKNOWN_OPCODE. The test isn't asserting *which*
  // status code; it asserts the call doesn't crash and returns a
  // non-SUCCESS code (i.e., the base did something sensible).
  Aircraft a;
  std::vector<std::uint8_t> resp;
  const std::uint8_t rc = a.handleCommand(0x0FFF, apex::compat::rospan<std::uint8_t>{}, resp);

  // Whatever the base returns, it shouldn't be SUCCESS for a totally
  // unknown opcode. (CommandResult::SUCCESS == 0 by definition.)
  EXPECT_NE(rc, static_cast<std::uint8_t>(CommandResult::SUCCESS));
}
