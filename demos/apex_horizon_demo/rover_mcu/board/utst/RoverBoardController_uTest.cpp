/**
 * @file RoverBoardController_uTest.cpp
 * @brief The board controller and the wire codec, on the host.
 *
 * The board controller is the shared guidance law with a mode, target
 * adoption, a halt and the strobe around it. These tests close the
 * loop through a small bicycle model at the link rate so the leg
 * numbers the host tests pin (RoverControllerClosedLoop_uTest) hold
 * for what the board will run, and check that every frame survives
 * build -> parse byte for byte.
 */

#include "demos/apex_horizon_demo/rover_mcu/board/inc/RoverBoardController.hpp"
#include "demos/apex_horizon_demo/rover_mcu/board/inc/RoverBoardProtocol.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>

using appsim::rover_board::BoardCommand;
using appsim::rover_board::BoardHeartbeat;
using appsim::rover_board::BoardState;
using appsim::rover_board::BoardTunables;
using appsim::rover_board::MODE_HOLD;
using appsim::rover_board::MODE_TRAJECTORY;
using appsim::rover_board::MODE_WAYPOINT;
using appsim::rover_board::Opcode;
using appsim::rover_board::RoverBoardController;

namespace {

/// A bicycle plant at the link rate: the same physics the host plant
/// integrates (heading rate v tan(delta) / L, speed toward the throttle
/// target under accel / decel limits).
struct Plant {
  double n{0.0}, e{0.0}, hdg{0.0}, v{0.0};
  static constexpr double L = 1.5, MAX_V = 8.0, ACC = 1.5, DEC = 2.0, DT = 0.05;
  void step(const BoardCommand& c) {
    const double TARGET = std::clamp(static_cast<double>(c.throttle_frac), 0.0, 1.0) * MAX_V;
    const double DV = TARGET - v;
    const double LIM = (DV >= 0.0 ? ACC : DEC) * DT;
    v += std::clamp(DV, -LIM, LIM);
    const double DELTA = std::clamp(static_cast<double>(c.steer_deg), -33.0, 33.0) * M_PI / 180.0;
    hdg = std::fmod(hdg + v * std::tan(DELTA) / L * 180.0 / M_PI * DT + 360.0, 360.0);
    n += v * std::cos(hdg * M_PI / 180.0) * DT;
    e += v * std::sin(hdg * M_PI / 180.0) * DT;
  }
  BoardState state(uint16_t seq) const {
    BoardState s{};
    s.north_m = static_cast<float>(n);
    s.east_m = static_cast<float>(e);
    s.heading_deg = static_cast<float>(hdg);
    s.speed_m_s = static_cast<float>(v);
    s.max_speed_m_s = static_cast<float>(MAX_V);
    s.seq_num = seq;
    return s;
  }
};

struct Rig {
  Plant plant;
  RoverBoardController ctl;
  uint16_t seq{0};
  uint16_t target_seq{0};
  uint8_t target_valid{0};
  float tn{0.0F}, te{0.0F}, sn{0.0F}, se{0.0F}; ///< The leg the host resolved.
  BoardCommand last{};
  Rig() {
    BoardTunables t{};
    t.step_hz = 20;
    ctl.setTunables(t);
  }
  /// One link tick: state to the board, its command back into the plant.
  void tick(uint8_t mode, uint8_t halt = 0) {
    BoardState s = plant.state(++seq);
    s.mode = mode;
    s.target_valid = target_valid;
    s.target_north_m = tn;
    s.target_east_m = te;
    s.start_north_m = sn;
    s.start_east_m = se;
    s.target_seq = target_seq;
    s.halt = halt;
    ctl.updateState(s);
    last = ctl.step();
    plant.step(last);
  }
  /// Resolve a leg the way the host controller does: from here to here + (dn, de).
  void setLegRel(float dn, float de) {
    sn = static_cast<float>(plant.n);
    se = static_cast<float>(plant.e);
    tn = sn + dn;
    te = se + de;
    target_valid = 1;
    ++target_seq;
  }
  void setLegAbs(float n, float e) {
    sn = static_cast<float>(plant.n);
    se = static_cast<float>(plant.e);
    tn = n;
    te = e;
    target_valid = 1;
    ++target_seq;
  }
  int runLeg(float dn, float de, int max_ticks = 600) {
    setLegRel(dn, de);
    int t = 0;
    tick(MODE_WAYPOINT);
    while (last.arrived == 0u && ++t < max_ticks) {
      tick(MODE_WAYPOINT);
    }
    return t;
  }
};

} // namespace

/* ----------------------------- Codec ----------------------------- */

TEST(RoverBoardProtocol, FramesRoundTripAndACorruptByteIsRefused) {
  BoardState s{};
  s.north_m = 12.5F;
  s.heading_deg = 271.0F;
  s.target_valid = 1;
  s.target_north_m = 22.5F;
  s.target_seq = 7;
  s.seq_num = 41;
  uint8_t buf[appsim::rover_board::MAX_FRAME_PAYLOAD]{};
  const size_t LEN =
      appsim::rover_board::buildFrame(Opcode::STATE_UPDATE, &s, sizeof(s), buf, sizeof(buf));
  ASSERT_EQ(LEN, 1u + sizeof(BoardState) + 2u);
  auto f = appsim::rover_board::parseFrame(buf, LEN);
  ASSERT_TRUE(f.ok);
  EXPECT_EQ(f.opcode, Opcode::STATE_UPDATE);
  ASSERT_EQ(f.payload_len, sizeof(BoardState));
  BoardState back{};
  std::memcpy(&back, f.payload, sizeof(back));
  EXPECT_EQ(back.north_m, 12.5F);
  EXPECT_EQ(back.heading_deg, 271.0F);
  EXPECT_EQ(back.target_north_m, 22.5F);
  EXPECT_EQ(back.target_seq, 7u);
  EXPECT_EQ(back.seq_num, 41u);
  buf[5] ^= 0x40u;
  EXPECT_FALSE(appsim::rover_board::parseFrame(buf, LEN).ok) << "CRC catches a flipped bit";
  EXPECT_FALSE(appsim::rover_board::parseFrame(buf, 2).ok) << "too short";

  BoardHeartbeat hb{};
  hb.cycle_count = 100000;
  hb.load_pct = 3;
  const size_t HL =
      appsim::rover_board::buildFrame(Opcode::HEARTBEAT, &hb, sizeof(hb), buf, sizeof(buf));
  auto h = appsim::rover_board::parseFrame(buf, HL);
  ASSERT_TRUE(h.ok);
  EXPECT_EQ(h.opcode, Opcode::HEARTBEAT);
  EXPECT_EQ(h.payload_len, sizeof(BoardHeartbeat));
  // CRC-16/XMODEM check value: "123456789" -> 0x31C3.
  const uint8_t CHECK[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  EXPECT_EQ(appsim::rover_board::crc16Xmodem(CHECK, sizeof(CHECK)), 0x31C3u);
}

/* ----------------------------- Modes ----------------------------- */

TEST(RoverBoardController, HoldIsZeroAndTrajectoryIsTheConstantSteer) {
  Rig rig;
  rig.tick(MODE_HOLD);
  EXPECT_EQ(rig.last.mode, MODE_HOLD);
  EXPECT_EQ(rig.last.steer_deg, 0.0F);
  EXPECT_EQ(rig.last.throttle_frac, 0.0F);
  rig.tick(MODE_TRAJECTORY);
  EXPECT_EQ(rig.last.mode, MODE_TRAJECTORY);
  EXPECT_NEAR(rig.last.steer_deg, 5.0F, 1e-6F);
  EXPECT_NEAR(rig.last.throttle_frac, 0.6F, 1e-6F);
  EXPECT_EQ(rig.last.ack_seq, rig.seq) << "each command answers the state it was computed on";
}

TEST(RoverBoardController, NoStateOrAHaltMeansZeroDrive) {
  RoverBoardController ctl;
  BoardCommand c = ctl.step();
  EXPECT_EQ(c.throttle_frac, 0.0F);
  Rig rig;
  rig.tick(MODE_TRAJECTORY, 1u);
  EXPECT_EQ(rig.last.throttle_frac, 0.0F) << "halted: the board drives nothing";
  EXPECT_EQ(rig.last.steer_deg, 0.0F);
}

/* ----------------------------- Legs (the host tests' numbers) ----------------------------- */

TEST(RoverBoardController, LegNorthRampsCruisesBrakesAndLatches) {
  Rig rig;
  double vmax = 0.0;
  rig.setLegRel(20.0F, 0.0F);
  rig.tick(MODE_WAYPOINT);
  int t = 0;
  while (rig.last.arrived == 0u && ++t < 600) {
    rig.tick(MODE_WAYPOINT);
    vmax = std::max(vmax, rig.plant.v);
  }
  ASSERT_LT(t, 600);
  EXPECT_NEAR(vmax, 3.0, 0.05) << "cruise";
  EXPECT_NEAR(rig.plant.n, 20.0, 0.3);
  EXPECT_LT(rig.plant.n, 20.3) << "no overshoot";
  EXPECT_EQ(rig.last.target_seq, rig.target_seq);
  // Latched: the same leg keeps answering arrived with zero drive.
  rig.tick(MODE_WAYPOINT);
  EXPECT_EQ(rig.last.arrived, 1u);
  EXPECT_EQ(rig.last.throttle_frac, 0.0F);
}

TEST(RoverBoardController, RepeatedLegsFromAnOffAxisHeadingEndSquare) {
  Rig rig;
  rig.plant.hdg = 116.0;
  const double E0 = rig.plant.e;
  for (int leg = 1; leg <= 5; ++leg) {
    const double N0 = rig.plant.n;
    const int T = rig.runLeg(10.0F, 0.0F);
    ASSERT_LT(T, 600) << "leg " << leg;
    const double H_ERR = std::fabs(std::fmod(rig.plant.hdg + 180.0, 360.0) - 180.0);
    EXPECT_NEAR(rig.plant.n - N0, 10.0, 0.3) << "leg " << leg;
    EXPECT_NEAR(rig.plant.e, E0, 0.3) << "leg " << leg << " lands on the line";
    EXPECT_LT(H_ERR, (leg == 1) ? 10.0 : 2.0) << "leg " << leg << " ends square";
    if (leg >= 2) {
      EXPECT_LT(T, 100) << "leg " << leg << " straight: 10 m in under 5 s";
    }
  }
}

TEST(RoverBoardController, AbsoluteTargetAndYawBoundHold) {
  Rig rig;
  rig.plant.hdg = 90.0;
  rig.plant.n = 30.0;
  rig.plant.e = 40.0;
  rig.setLegAbs(0.0F, 0.0F);
  rig.tick(MODE_WAYPOINT);
  double max_rate = 0.0, prev = rig.plant.hdg;
  int t = 0;
  while (rig.last.arrived == 0u && ++t < 1200) {
    rig.tick(MODE_WAYPOINT);
    double dh = rig.plant.hdg - prev;
    if (dh > 180.0)
      dh -= 360.0;
    if (dh < -180.0)
      dh += 360.0;
    prev = rig.plant.hdg;
    max_rate = std::max(max_rate, std::fabs(dh) / Plant::DT);
  }
  ASSERT_LT(t, 1200);
  EXPECT_NEAR(rig.plant.n, 0.0, 0.5);
  EXPECT_NEAR(rig.plant.e, 0.0, 0.5);
  EXPECT_LE(max_rate, 15.0 + 0.5) << "the 15 deg/s bound holds on every tick";
}

TEST(RoverBoardController, AResetMidLegKeepsTheSameLine) {
  // The board restarts halfway along a 20 m leg: a fresh controller
  // reads the same leg from the next frame and finishes it where the
  // uninterrupted leg ends, instead of starting 20 m from here.
  Rig rig;
  rig.setLegRel(20.0F, 0.0F);
  int guard = 0;
  while (rig.plant.n < 10.0 && ++guard < 400) {
    rig.tick(MODE_WAYPOINT);
  }
  ASSERT_GE(rig.plant.n, 10.0) << "halfway";
  ASSERT_EQ(rig.last.arrived, 0u);
  rig.ctl = RoverBoardController{};
  BoardTunables t{};
  t.step_hz = 20;
  rig.ctl.setTunables(t);
  int n = 0;
  rig.tick(MODE_WAYPOINT);
  while (rig.last.arrived == 0u && ++n < 600) {
    rig.tick(MODE_WAYPOINT);
  }
  ASSERT_LT(n, 600);
  EXPECT_NEAR(rig.plant.n, 20.0, 0.3) << "the leg ends where it always did";
}

TEST(RoverBoardController, ShortLegsFromEveryHeadingArriveOnTheTarget) {
  // 2, 4 and 6 m legs (shorter than a turn-around loop) entered from 24
  // headings: every one arrives, in bounded time, within the arrival
  // rule's 0.5 m of its target, with the heading-rate bound held.
  for (const double LEG : {2.0, 4.0, 6.0}) {
    for (int k = 0; k < 24; ++k) {
      Rig rig;
      rig.plant.hdg = 15.0 * k;
      rig.setLegAbs(static_cast<float>(LEG), 0.0F);
      rig.tick(MODE_WAYPOINT);
      int t = 0;
      double max_rate = 0.0, prev = rig.plant.hdg;
      while (rig.last.arrived == 0u && ++t < 1600) {
        rig.tick(MODE_WAYPOINT);
        double dh = rig.plant.hdg - prev;
        if (dh > 180.0)
          dh -= 360.0;
        if (dh < -180.0)
          dh += 360.0;
        prev = rig.plant.hdg;
        max_rate = std::max(max_rate, std::fabs(dh) / Plant::DT);
      }
      const double MISS = std::hypot(rig.plant.n - LEG, rig.plant.e);
      EXPECT_LT(t, 1600) << "leg " << LEG << " from " << 15.0 * k << " deg never arrived";
      EXPECT_LT(MISS, 0.5) << "leg " << LEG << " from " << 15.0 * k << " deg ends " << MISS
                           << " m off after " << t << " ticks";
      EXPECT_LE(max_rate, 15.5) << "leg " << LEG << " from " << 15.0 * k << " deg";
    }
  }
}

/* ----------------------------- Strobe ----------------------------- */

TEST(RoverBoardController, StrobeFollowsTheRateCodesAtTheStepRate) {
  Rig rig; // 20 Hz
  // Lamp 1 red at 1 Hz (code 2): 20 steps per period, on for the first 10.
  int on = 0;
  for (int i = 0; i < 40; ++i) {
    BoardState s = rig.plant.state(++rig.seq);
    s.led1_colour = 1;
    s.led1_rate = 2;
    s.led2_colour = 3;
    s.led2_rate = 0; // steady blue
    rig.ctl.updateState(s);
    const BoardCommand c = rig.ctl.step();
    on += (c.led_bits & 0x01u) != 0u ? 1 : 0;
    EXPECT_EQ(c.led_bits & 0x02u, 0x02u) << "steady lamp is on";
  }
  EXPECT_EQ(on, 20) << "1 Hz over 2 s at 20 Hz: on half the steps";
}
