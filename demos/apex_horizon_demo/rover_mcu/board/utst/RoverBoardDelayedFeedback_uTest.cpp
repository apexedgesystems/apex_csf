/**
 * @file RoverBoardDelayedFeedback_uTest.cpp
 * @brief The shipped board composition, at its real rates and delay.
 *
 * The board's command does not reach the plant on the state it was
 * computed from. `RoverBoardLink::linkStep` drains the board's reply
 * before it sends the current state, so a command crosses the link and
 * takes effect a link tick after the pose it answers -- and the plant
 * takes five 100 Hz steps inside that tick. The law's own tests run the
 * plant and the controller at one rate with the command applied
 * immediately, which cannot see what that lead does.
 *
 * This rig is the real GroundVehicle at 100 Hz, the real
 * RoverController in board mode, and the real RoverBoardController at
 * 20 Hz, with the link's drain-before-send ordering reproduced.
 *
 * Coverage:
 *  - A target behind the rover arrives rather than driving away: the
 *    reversal keeps its committed direction when the bearing error sits
 *    on the 180-degree wrap, where a delayed command lands either side
 *    of it.
 *  - A target admitted between the arrival ring and half a metre
 *    arrives: short legs keep a line, so they are driven through the
 *    keyhole instead of circling a point inside the turning circle.
 *  - The heading rate holds its bound on every plant step, including
 *    while a command computed at one speed lands at a higher one.
 *  - A reply the link accepts later than one tick -- held a tick, the next
 *    reply lost, the link still UP -- lands at a speed the board never
 *    saw; the controller re-bounds the lock at consumption, so the rate
 *    holds on every plant step through that too.
 */

#include "demos/apex_horizon_demo/ground_vehicle/inc/GroundVehicle.hpp"
#include "demos/apex_horizon_demo/rover_controller/inc/RoverController.hpp"
#include "demos/apex_horizon_demo/rover_mcu/board/inc/RoverBoardController.hpp"
#include "demos/apex_horizon_demo/rover_mcu/board/inc/RoverBoardProtocol.hpp"

#include "src/sim/environment/celestial_body/inc/CelestialBody.hpp"
#include "src/sim/environment/factory/inc/Body.hpp"
#include "src/sim/environment/factory/inc/EnvironmentFidelity.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

using appsim::ground_vehicle::GroundVehicle;
using appsim::rover_board::BoardCommand;
using appsim::rover_board::BoardLinkSnapshot;
using appsim::rover_board::BoardState;
using appsim::rover_board::BoardTunables;
using appsim::rover_board::LINK_UP;
using appsim::rover_board::RoverBoardController;
using appsim::rover_controller::DriveMode;
using appsim::rover_controller::RoverController;
using sim::environment::AtmosphereFidelity;
using sim::environment::Body;
using sim::environment::GravityFidelity;
using sim::environment::TerrainFidelity;
using sim::environment::celestial_body::CelestialBody;
using sim::environment::celestial_body::CelestialBodyTunables;

namespace {

constexpr int PLANT_STEPS_PER_LINK_TICK = 5; ///< 100 Hz plant, 20 Hz link.
constexpr double PLANT_DT_S = 0.01;

/// The shipped composition with the link's one-tick command lead.
class LinkedRig {
public:
  explicit LinkedRig(double heading_deg) {
    body_.tunables().get() = analyticEarth();
    EXPECT_EQ(body_.init(), 0u);

    auto& p = rover_.tunables().get();
    p.step_hz = 100u;
    p.max_speed_m_s = 8.0;
    p.throttle_default = 0.5;
    p.steer_rate_deg_s = 0.0;
    p.init_lat_deg = ANCHOR_LAT_DEG;
    p.init_lon_deg = ANCHOR_LON_DEG;
    p.init_heading_deg = heading_deg;
    p.max_slope_deg = 89.0;
    p.lidar_n_rays = 4u;
    p.lidar_max_range_m = 100.0;
    p.lidar_fov_deg = 90.0;
    p.lidar_step_m = 5.0;
    rover_.setBody(&body_);
    EXPECT_EQ(rover_.init(), 0u);

    auto& cp = controller_.tunables().get();
    cp.anchor_lat_deg = ANCHOR_LAT_DEG;
    cp.anchor_lon_deg = ANCHOR_LON_DEG;
    EXPECT_EQ(controller_.init(), 0u);
    controller_.setVehicle(&rover_);
    controller_.setBoardLink(&snapshot_);
    rover_.setDriveCommand(controller_.driveCommand());

    BoardTunables bt{};
    bt.step_hz = 20u;
    board_.setTunables(bt);

    snapshot_.enabled = 1u;
    snapshot_.link_state = LINK_UP;

    // First plant step latches the pose from the tunables, and one link
    // tick brings the controller up: its first step adopts the booted
    // mode, so a target set before that would be overwritten.
    (void)rover_.vehicleStep();
    linkTick();
  }

  void setTargetAbs(double north_m, double east_m) noexcept {
    controller_.setMode(DriveMode::WAYPOINT);
    controller_.setTargetAbs(north_m, east_m);
  }

  /**
   * @brief One link tick in the shipped order, plus the plant's steps.
   *
   * Drain before send: the command the controller forwards now is the
   * one the board computed from the state sent on the previous tick.
   */
  /// Hold the reply the board computes on the coming tick one extra link
  /// tick, and lose the one after it: the delivery the link accepts while
  /// UP with one sequence gap. The held reply lands two ticks after the
  /// state it answers instead of one, and stays in force through the gap.
  void holdNextReply() noexcept { armHold_ = true; }

  void linkTick() noexcept {
    // Drain before send. A held reply: on the tick after it was computed
    // nothing new lands and the previous command stays in force; on the
    // tick after that the held reply lands, two ticks old.
    if (hold_ == 2) {
      hold_ = 1;
      dropNext_ = true; // The reply computed this tick is the lost one.
    } else if (hold_ == 1) {
      hold_ = 0;
      snapshot_.cmd = held_;
      snapshot_.board_tick = held_.seq_num;
      pending_ = held_; // The last received command holds through the gap.
    } else {
      snapshot_.cmd = pending_;
      snapshot_.board_tick = pending_.seq_num;
    }
    (void)controller_.controllerStep();

    BoardState st{};
    double north = 0.0;
    double east = 0.0;
    controller_.gridPosition(north, east);
    const auto& CS = controller_.controllerState();
    st.north_m = static_cast<float>(north);
    st.east_m = static_cast<float>(east);
    st.heading_deg = static_cast<float>(rover_.telemetry().heading_deg);
    st.speed_m_s = static_cast<float>(rover_.telemetry().speed_m_s);
    st.max_speed_m_s = static_cast<float>(rover_.tunables_const().max_speed_m_s);
    st.target_north_m = static_cast<float>(CS.target_north_m);
    st.target_east_m = static_cast<float>(CS.target_east_m);
    st.start_north_m = static_cast<float>(CS.leg_start_north_m);
    st.start_east_m = static_cast<float>(CS.leg_start_east_m);
    st.mode = static_cast<std::uint8_t>(controller_.mode());
    st.target_valid = CS.target_valid;
    st.halt = (rover_.vehicleState().commanded_halt != 0u) ? 1u : 0u;
    st.target_seq = CS.target_seq;
    st.seq_num = ++tx_seq_;
    board_.updateState(st);
    const BoardCommand REPLY = board_.step();
    if (armHold_) {
      armHold_ = false;
      held_ = REPLY; // Lands two ticks from now.
      hold_ = 2;
    } else if (dropNext_) {
      dropNext_ = false; // Never lands.
    } else {
      pending_ = REPLY; // Lands next tick.
    }

    for (int i = 0; i < PLANT_STEPS_PER_LINK_TICK; ++i) {
      const double BEFORE = rover_.telemetry().heading_deg;
      (void)rover_.vehicleStep();
      double dh = rover_.telemetry().heading_deg - BEFORE;
      if (dh > 180.0) {
        dh -= 360.0;
      }
      if (dh < -180.0) {
        dh += 360.0;
      }
      max_yaw_rate_deg_s_ = std::fmax(max_yaw_rate_deg_s_, std::fabs(dh) / PLANT_DT_S);
    }
  }

  /// Distance from the commanded target, in metres on the grid.
  [[nodiscard]] double missM() const noexcept {
    double north = 0.0;
    double east = 0.0;
    controller_.gridPosition(north, east);
    const auto& CS = controller_.controllerState();
    return std::hypot(north - CS.target_north_m, east - CS.target_east_m);
  }

  [[nodiscard]] bool arrived() const noexcept {
    return controller_.controllerOutput().arrived != 0u;
  }
  [[nodiscard]] double maxYawRateDegS() const noexcept { return max_yaw_rate_deg_s_; }

private:
  static constexpr double ANCHOR_LAT_DEG = 39.5;
  static constexpr double ANCHOR_LON_DEG = -105.5;

  static CelestialBodyTunables analyticEarth() {
    CelestialBodyTunables t{};
    t.body = Body::EARTH;
    t.gravity_fidelity = GravityFidelity::J2;
    t.terrain_fidelity = TerrainFidelity::ELLIPSOID;
    t.atmosphere_fidelity = AtmosphereFidelity::EXPONENTIAL;
    return t;
  }

  CelestialBody body_{};
  GroundVehicle rover_{};
  RoverController controller_{};
  RoverBoardController board_{};
  BoardLinkSnapshot snapshot_{};
  BoardCommand pending_{};
  BoardCommand held_{};
  int hold_{0};
  bool armHold_{false};
  bool dropNext_{false};
  std::uint16_t tx_seq_{0};
  double max_yaw_rate_deg_s_{0.0};
};

/// Run link ticks until arrival or the budget runs out; returns the ticks used.
int runToArrival(LinkedRig& rig, int budget) {
  int t = 0;
  while (!rig.arrived() && ++t < budget) {
    rig.linkTick();
  }
  return t;
}

} // namespace

/**
 * @test A target behind the rover arrives instead of driving away.
 */
TEST(RoverBoardDelayedFeedback, TargetBehindArrivesRatherThanDrivingAway) {
  // 1.524 m north from heading 45 deg: the leg's line puts the keyhole
  // aim point behind the rover, where the bearing error rides the
  // 180-degree wrap and a delayed command lands either side of it.
  LinkedRig rig(45.0);
  rig.setTargetAbs(1.524, 0.0);

  const int TICKS = runToArrival(rig, 2400); // 2 minutes of link ticks.
  EXPECT_TRUE(rig.arrived()) << "still driving after " << TICKS << " link ticks, " << rig.missM()
                             << " m from the target";
  EXPECT_LT(rig.missM(), 0.5);
}

/**
 * @test A target admitted inside half a metre arrives.
 */
TEST(RoverBoardDelayedFeedback, ShortAdmittedLegArrives) {
  // 0.3 m north from heading 90 deg: abeam, inside the turning circle
  // and outside the 0.2 m arrival ring.
  LinkedRig rig(90.0);
  rig.setTargetAbs(0.3, 0.0);

  const int TICKS = runToArrival(rig, 2400);
  EXPECT_TRUE(rig.arrived()) << "still driving after " << TICKS << " link ticks, " << rig.missM()
                             << " m from the target";
  EXPECT_LT(rig.missM(), 0.5);
}

/**
 * @test The heading rate holds its bound on every plant step.
 */
TEST(RoverBoardDelayedFeedback, YawRateHoldsItsBoundAcrossTheDelay) {
  // 3 m north from heading 60 deg: the rover is still accelerating
  // while the command computed at the lower speed crosses the link.
  // Every leg from every heading, measured on the plant's own steps. The
  // violations are not rare: before the bound allowed for the lead, 1 m
  // and 2 m legs from a dozen of these headings crossed it, the worst at
  // 16.0 deg/s.
  for (const double DIST : {1.0, 2.0, 3.0}) {
    for (int k = 0; k < 24; ++k) {
      LinkedRig rig(15.0 * k);
      rig.setTargetAbs(DIST, 0.0);
      (void)runToArrival(rig, 2400);
      EXPECT_LE(rig.maxYawRateDegS(), 15.0)
          << DIST << " m leg from " << 15.0 * k << " deg turned at " << rig.maxYawRateDegS()
          << " deg/s";
    }
  }
}

/**
 * @test A late reply the link still accepts lands within the yaw bound.
 */
TEST(RoverBoardDelayedFeedback, LateReplyStillAcceptedLandsWithinTheYawBound) {
  // Every leg from every heading, with a reply held a tick and the next
  // lost every fourth tick through the first two seconds of the leg --
  // the whole run-up from rest to cruise, so some hold lands while the
  // rover is turning at its lock with speed still rising. The board
  // bounded that reply against the speed it sampled plus one tick;
  // landing two ticks late it meets a faster plant, and only the bound
  // re-applied at consumption keeps the rate inside 15 deg/s.
  for (const double DIST : {2.0, 3.0, 4.0, 6.0}) {
    for (int k = 0; k < 24; ++k) {
      LinkedRig rig(15.0 * k);
      rig.setTargetAbs(DIST, 0.0);
      for (int t = 0; t < 2400 && !rig.arrived(); ++t) {
        if (t >= 2 && t <= 40 && (t % 4) == 2) {
          rig.holdNextReply();
        }
        rig.linkTick();
      }
      EXPECT_LE(rig.maxYawRateDegS(), 15.0)
          << DIST << " m leg from " << 15.0 * k << " deg turned at " << rig.maxYawRateDegS()
          << " deg/s with a late reply";
    }
  }
}

/**
 * @test A retarget during the run-up, with its first turn reply late,
 *       lands within the yaw bound.
 */
TEST(RoverBoardDelayedFeedback, LateTurnReplyAfterARetargetMidRunUpLandsWithinTheYawBound) {
  // The case that breaks a board-only bound: the rover is cruising north
  // and still accelerating when a target abeam arrives. The board's first
  // reply for it turns hard at the lock it computed for the speed it
  // sampled plus one tick; that reply is held a tick and the next lost,
  // and the plant, still under the previous command, keeps accelerating
  // through both -- so the turn lands at a speed well above the board's
  // allowance. Swept over when the retarget happens in the run-up.
  for (int retargetTick = 6; retargetTick <= 40; retargetTick += 2) {
    for (const double SIDE : {10.0, -10.0}) {
      LinkedRig rig(0.0);
      rig.setTargetAbs(30.0, 0.0);
      for (int t = 0; t < 2400 && !rig.arrived(); ++t) {
        if (t == retargetTick) {
          rig.setTargetAbs(30.0, SIDE); // Abeam of the run so far.
          rig.holdNextReply();          // The first turn reply is the late one.
        }
        rig.linkTick();
        if (t > retargetTick + 60) {
          break; // The turn is over; the rest of the leg adds nothing.
        }
      }
      EXPECT_LE(rig.maxYawRateDegS(), 15.0)
          << "retarget at tick " << retargetTick << " to the " << (SIDE > 0 ? "east" : "west")
          << " turned at " << rig.maxYawRateDegS() << " deg/s with its first reply late";
    }
  }
}
