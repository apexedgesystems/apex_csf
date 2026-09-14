/**
 * @file RoverBoardLink_uTest.cpp
 * @brief The host link against a virtual board on a pseudo-terminal.
 *
 * A PtyPair stands in for the ST-Link virtual COM port: the link driver
 * opens the slave end exactly as it opens /dev/ttyACM0, and a virtual
 * board on the master end runs the RoverBoardController the firmware
 * runs. The loop closes through the real plant and the host controller
 * in board mode, so these tests cover the wire, the link judgement, the
 * frame's board bytes and the drive the plant actually receives.
 */

#include "demos/apex_horizon_demo/ground_vehicle/inc/GroundVehicleCommand.hpp"
#include "demos/apex_horizon_demo/rover_mcu/board/inc/RoverBoardController.hpp"
#include "demos/apex_horizon_demo/rover_mcu/board_link/inc/RoverBoardLink.hpp"
#include "src/sim/environment/celestial_body/inc/CelestialBody.hpp"
#include "src/sim/environment/factory/inc/Body.hpp"
#include "src/sim/environment/factory/inc/EnvironmentFidelity.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/PtyPair.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

using apex::protocols::serial::uart::PtyPair;
using appsim::ground_vehicle::GroundVehicle;
using appsim::ground_vehicle::RoverCmdSetTarget;
using appsim::ground_vehicle::RoverOpcode;
using appsim::rover_board::BoardCommand;
using appsim::rover_board::BoardHeartbeat;
using appsim::rover_board::BoardState;
using appsim::rover_board::BoardTunables;
using appsim::rover_board::LINK_LOST;
using appsim::rover_board::LINK_NEVER;
using appsim::rover_board::LINK_UP;
using appsim::rover_board::MAX_FRAME_PAYLOAD;
using appsim::rover_board::MAX_SLIP_ENCODED;
using appsim::rover_board::Opcode;
using appsim::rover_board::RoverBoardController;
using appsim::rover_board::RoverBoardLink;
using appsim::rover_controller::DriveMode;
using appsim::rover_controller::RoverController;
using sim::environment::AtmosphereFidelity;
using sim::environment::Body;
using sim::environment::GravityFidelity;
using sim::environment::TerrainFidelity;
using sim::environment::celestial_body::CelestialBody;
using sim::environment::celestial_body::CelestialBodyTunables;
using UartStatus = apex::protocols::serial::uart::Status;
namespace fb = appsim::ground_vehicle;
namespace slip = apex::protocols::slip;

namespace {

CelestialBodyTunables analyticEarth() {
  CelestialBodyTunables t{};
  t.body = Body::EARTH;
  t.gravity_fidelity = GravityFidelity::J2;
  t.terrain_fidelity = TerrainFidelity::ELLIPSOID;
  t.atmosphere_fidelity = AtmosphereFidelity::EXPONENTIAL;
  return t;
}

/// The board on the master end of a pseudo-terminal: answers every
/// STATE_UPDATE with the command RoverBoardController computes.
struct VirtualBoard {
  PtyPair pty;
  RoverBoardController ctl;
  slip::DecodeState ds{};
  slip::DecodeConfig dc{};
  std::array<std::uint8_t, 512> raw{};
  std::array<std::uint8_t, MAX_FRAME_PAYLOAD> dec{};
  std::array<std::uint8_t, MAX_FRAME_PAYLOAD> frame{};
  std::array<std::uint8_t, MAX_SLIP_ENCODED> encoded{};
  bool answering{true};
  std::uint16_t seq{0};
  int states{0};

  VirtualBoard() {
    dc.maxFrameSize = MAX_FRAME_PAYLOAD;
    dc.allowEmptyFrame = false;
    dc.dropUntilEnd = true;
    dc.requireTrailingEnd = true;
    BoardTunables t{};
    t.step_hz = 20;
    ctl.setTunables(t);
  }

  void writeRaw(const std::uint8_t* data, std::size_t len) {
    std::size_t written = 0;
    (void)pty.writeMaster(data, len, written, 100);
  }

  void send(Opcode op, const void* payload, std::size_t len) {
    const std::size_t N =
        appsim::rover_board::buildFrame(op, payload, len, frame.data(), frame.size());
    auto enc = slip::encode({frame.data(), N}, encoded.data(), encoded.size());
    writeRaw(encoded.data(), enc.bytesProduced);
  }

  void heartbeat(std::uint8_t load) {
    BoardHeartbeat hb{};
    hb.cycle_count = 1000;
    hb.step_count = ctl.stepCount();
    hb.overhead_us = 40;
    hb.load_pct = load;
    send(Opcode::HEARTBEAT, &hb, sizeof(hb));
  }

  void service() {
    for (int guard = 0; guard < 8; ++guard) {
      std::size_t n = 0;
      if (pty.readMaster(raw.data(), raw.size(), n, 0) != UartStatus::SUCCESS || n == 0u) {
        return;
      }
      std::size_t pos = 0;
      while (pos < n) {
        const std::size_t PREV = ds.frameLen;
        auto r = slip::decodeChunk(ds, dc, {raw.data() + pos, n - pos}, dec.data() + PREV,
                                   dec.size() - PREV);
        pos += r.bytesConsumed;
        if (r.frameCompleted) {
          const auto F = appsim::rover_board::parseFrame(dec.data(), PREV + r.bytesProduced);
          if (F.ok && F.opcode == Opcode::STATE_UPDATE && F.payload_len == sizeof(BoardState)) {
            BoardState s{};
            std::memcpy(&s, F.payload, sizeof(s));
            ++states;
            ctl.updateState(s);
            BoardCommand c = ctl.step();
            c.seq_num = ++seq;
            if (answering) {
              send(Opcode::CONTROL_CMD, &c, sizeof(c));
            }
          }
        }
        if (r.status == slip::Status::OUTPUT_FULL) {
          ds.reset();
          continue;
        }
        if (r.bytesConsumed == 0u) {
          break;
        }
      }
    }
  }
};

/// Plant + host controller in board mode + link, against the virtual board.
struct Rig {
  CelestialBody earth;
  GroundVehicle rover;
  RoverController ctl;
  RoverBoardLink link;
  VirtualBoard board;

  explicit Rig(bool enabled = true) {
    earth.tunables().set(analyticEarth());
    EXPECT_EQ(earth.init(), 0u);
    auto& rp = rover.tunables().get();
    rp.init_lat_deg = 39.5;
    rp.init_lon_deg = -105.5;
    rp.init_heading_deg = 0.0;
    rp.step_hz = 20; // one plant step per link tick
    rover.setBody(&earth);
    ctl.setVehicle(&rover);
    ctl.setBoardLink(&link.snapshot());
    rover.setDriveCommand(ctl.driveCommand());
    link.setSources(&rover, &ctl);
    EXPECT_TRUE(board.pty.open() == UartStatus::SUCCESS);
    auto& lp = link.tunables().get();
    std::snprintf(lp.device_path, sizeof(lp.device_path), "%s", board.pty.slavePath());
    lp.enabled = enabled ? 1u : 0u;
    lp.link_timeout_ms = 150;
    (void)link.telemetryTick(); // opens the port, as the 1 Hz task does
  }

  /// One link tick in scheduler order: link, (the board answers), controller, plant.
  void tick() {
    (void)link.linkStep();
    board.service();
    (void)ctl.controllerStep();
    (void)rover.vehicleStep();
  }

  std::uint8_t command(RoverOpcode op, const std::vector<std::uint8_t>& bytes) {
    apex::compat::rospan<std::uint8_t> payload(bytes.data(), bytes.size());
    std::vector<std::uint8_t> resp;
    return rover.handleCommand(static_cast<std::uint16_t>(op), payload, resp);
  }

  static std::vector<std::uint8_t> target(float a, float b) {
    RoverCmdSetTarget t{a, b};
    std::vector<std::uint8_t> v(sizeof(t));
    std::memcpy(v.data(), &t, sizeof(t));
    return v;
  }

  void northEast(double& n, double& e) const { ctl.gridPosition(n, e); }
};

} // namespace

/* ----------------------------- Link judgement ----------------------------- */

TEST(RoverBoardLink, ComesUpAndCarriesTheBoardsLoadAndTickOntoTheFrame) {
  Rig rig;
  EXPECT_EQ(rig.link.linkState().uart_open, 1u) << "the slave end opens like /dev/ttyACM0";
  EXPECT_EQ(rig.link.linkState().link_state, LINK_NEVER);

  rig.tick();
  EXPECT_EQ(rig.board.states, 1) << "the state frame crossed the wire";
  EXPECT_EQ(rig.link.linkState().link_state, LINK_NEVER) << "the reply is read on the next tick";
  EXPECT_EQ(rig.ctl.controllerOutput().throttle_frac, 0.0) << "no board yet: zero drive";
  EXPECT_EQ(rig.ctl.controllerOutput().board_link, LINK_NEVER);

  rig.board.heartbeat(7);
  for (int i = 0; i < 10; ++i) {
    rig.tick();
  }
  const auto& s = rig.link.linkState();
  EXPECT_EQ(s.link_state, LINK_UP);
  EXPECT_EQ(s.heartbeats, 1u);
  EXPECT_EQ(s.load_pct, 7u);
  EXPECT_GE(s.rx_count, 10u);
  EXPECT_EQ(s.seq_gaps, 0u);
  EXPECT_EQ(s.crc_errors, 0u);
  const auto* frame = rig.rover.frameBytes();
  EXPECT_EQ(frame[fb::FB_BOARD_LINK], LINK_UP);
  EXPECT_EQ(frame[fb::FB_BOARD_LOAD_PCT], 7u);
  const std::uint16_t TICK =
      static_cast<std::uint16_t>(frame[fb::FB_BOARD_TICK_LO] | (frame[fb::FB_BOARD_TICK_HI] << 8));
  EXPECT_EQ(TICK, rig.link.snapshot().board_tick);
  EXPECT_GT(TICK, 0u);
}

TEST(RoverBoardLink, ABoardRebootIsARestartNotAGap) {
  Rig rig;
  for (int i = 0; i < 30; ++i) {
    rig.tick();
  }
  ASSERT_GE(rig.link.linkState().last_rx_seq, 29u);
  // The board reboots: its controller state and command numbering start over.
  rig.board.ctl = RoverBoardController{};
  BoardTunables t{};
  t.step_hz = 20;
  rig.board.ctl.setTunables(t);
  rig.board.seq = 0;
  for (int i = 0; i < 5; ++i) {
    rig.tick();
  }
  EXPECT_EQ(rig.link.linkState().board_restarts, 1u);
  EXPECT_EQ(rig.link.linkState().seq_gaps, 0u) << "numbering from 1 again is not lost frames";
  EXPECT_EQ(rig.link.linkState().link_state, LINK_UP);
}

TEST(RoverBoardLink, ACorruptFrameIsCountedAndIgnored) {
  Rig rig;
  rig.tick();
  // A SLIP-delimited frame whose CRC is wrong.
  const std::uint8_t BAD[] = {0xC0, 0x20, 0x01, 0x02, 0x03, 0x00, 0x00, 0xC0};
  rig.board.writeRaw(BAD, sizeof(BAD));
  rig.tick();
  rig.tick();
  EXPECT_EQ(rig.link.linkState().crc_errors, 1u);
  EXPECT_EQ(rig.link.linkState().link_state, LINK_UP) << "good frames keep flowing around it";
}

/* ----------------------------- Drive through the plant ----------------------------- */

TEST(RoverBoardLink, TheBoardDrivesALegAndAStaleArrivalNeverCompletesTheNext) {
  Rig rig;
  rig.tick();
  ASSERT_EQ(rig.command(RoverOpcode::SET_MODE, {2u}), 0u);
  ASSERT_EQ(rig.command(RoverOpcode::SET_TARGET_REL, Rig::target(20.0F, 0.0F)), 0u);
  double vmax = 0.0;
  int t = 0;
  while (rig.ctl.controllerOutput().arrived == 0u && ++t < 800) {
    rig.tick();
    vmax = std::max(vmax, rig.rover.telemetry().speed_m_s);
  }
  ASSERT_LT(t, 800) << "the board's leg arrives";
  double n = 0.0, e = 0.0;
  rig.northEast(n, e);
  EXPECT_NEAR(n, 20.0, 0.4);
  EXPECT_NEAR(e, 0.0, 0.3);
  EXPECT_NEAR(vmax, 3.0, 0.1) << "cruise";
  EXPECT_EQ(rig.link.linkState().seq_gaps, 0u);

  // A new target: the board's last reply still says arrived for the old
  // one, and the controller must not pass that on.
  ASSERT_EQ(rig.command(RoverOpcode::SET_TARGET_REL, Rig::target(10.0F, 0.0F)), 0u);
  rig.tick();
  EXPECT_EQ(rig.link.snapshot().cmd.arrived, 1u) << "the reply read this tick is for the old leg";
  EXPECT_EQ(rig.ctl.controllerOutput().arrived, 0u) << "stale arrival is not forwarded";
  int t2 = 0;
  while (rig.ctl.controllerOutput().arrived == 0u && ++t2 < 800) {
    rig.tick();
  }
  ASSERT_LT(t2, 800);
  rig.northEast(n, e);
  EXPECT_NEAR(n, 30.0, 0.5);
}

TEST(RoverBoardLink, SilenceIsLostAndZeroDriveThenTheLinkRecovers) {
  Rig rig;
  rig.tick();
  ASSERT_EQ(rig.command(RoverOpcode::SET_MODE, {2u}), 0u);
  ASSERT_EQ(rig.command(RoverOpcode::SET_TARGET_REL, Rig::target(80.0F, 0.0F)), 0u);
  for (int i = 0; i < 60; ++i) {
    rig.tick();
  }
  ASSERT_EQ(rig.link.linkState().link_state, LINK_UP);
  ASSERT_GT(rig.ctl.controllerOutput().throttle_frac, 0.0);

  rig.board.answering = false;
  for (int i = 0; i < 12; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    rig.tick();
  }
  EXPECT_EQ(rig.link.linkState().link_state, LINK_LOST);
  EXPECT_EQ(rig.ctl.controllerOutput().throttle_frac, 0.0) << "a silent board drives nothing";
  EXPECT_EQ(rig.ctl.controllerOutput().steer_angle_deg, 0.0);
  EXPECT_EQ(rig.rover.frameBytes()[fb::FB_BOARD_LINK], LINK_LOST);

  rig.board.answering = true;
  rig.tick();
  rig.tick();
  EXPECT_EQ(rig.link.linkState().link_state, LINK_UP);
  EXPECT_GT(rig.ctl.controllerOutput().throttle_frac, 0.0) << "the drive resumes with the link";
}

TEST(RoverBoardLink, DisabledLeavesTheHostLawAndNeverOpensThePort) {
  Rig rig(false);
  EXPECT_EQ(rig.link.linkState().uart_open, 0u);
  rig.tick(); // the controller's first step applies its boot mode
  rig.ctl.setMode(DriveMode::WAYPOINT);
  rig.ctl.setTargetRel(10.0, 0.0);
  for (int i = 0; i < 10; ++i) {
    rig.tick();
  }
  EXPECT_EQ(rig.board.states, 0) << "nothing crossed the wire";
  EXPECT_GT(rig.ctl.controllerOutput().throttle_frac, 0.0) << "the host law drives";
  EXPECT_EQ(rig.ctl.controllerOutput().board_link, LINK_NEVER);
  EXPECT_EQ(rig.ctl.controllerOutput().throttle_frac,
            rig.ctl.controllerOutput().shadow_throttle_frac);
}
