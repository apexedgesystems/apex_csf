/**
 * @file RoverBoardController.cpp
 * @brief Mode, adoption, halt and strobe around the shared guidance law.
 */

#include "demos/apex_horizon_demo/rover_mcu/board/inc/RoverBoardController.hpp"

namespace appsim {
namespace rover_board {

void RoverBoardController::updateState(const BoardState& s) noexcept {
  last_ = s;
  has_state_ = true;
  const uint8_t MODE = (s.mode <= MODE_WAYPOINT) ? s.mode : MODE_HOLD;
  if (MODE != MODE_WAYPOINT) {
    leg_.arrived = 0u;
  }
  mode_ = MODE;
  // A new leg (or the first frame after a reset) takes the host's line
  // as sent; the same leg keeps its arrival latch.
  if (s.target_valid == 0u) {
    leg_.valid = 0u;
    leg_.arrived = 0u;
  } else if (leg_.valid == 0u || s.target_seq != leg_seq_) {
    rover_controller::setLeg(
        leg_, static_cast<double>(s.target_north_m), static_cast<double>(s.target_east_m),
        static_cast<double>(s.start_north_m), static_cast<double>(s.start_east_m));
  }
  leg_seq_ = s.target_seq;
}

bool RoverBoardController::lampOn(uint8_t colour, uint8_t rate) const noexcept {
  if (colour == 0u) {
    return false;
  }
  if (rate == 0u) {
    return true;
  }
  // Rate codes 1..5 = 0.5, 1, 2, 5, 10 Hz: on for the first half of each period.
  static constexpr uint16_t PERIOD_MS[6] = {0, 2000, 1000, 500, 200, 100};
  const uint16_t CODE = (rate <= 5u) ? rate : 5u;
  const uint32_t PERIOD_STEPS = (static_cast<uint32_t>(PERIOD_MS[CODE]) * p_.step_hz) / 1000u;
  if (PERIOD_STEPS == 0u) {
    return true;
  }
  return (steps_ % PERIOD_STEPS) < (PERIOD_STEPS / 2u);
}

BoardCommand RoverBoardController::step() noexcept {
  BoardCommand cmd{};
  cmd.mode = mode_;
  cmd.target_seq = leg_seq_;
  cmd.ack_seq = last_.seq_num;
  if (has_state_) {
    cmd.led_bits = static_cast<uint8_t>((lampOn(last_.led1_colour, last_.led1_rate) ? 0x01u : 0u) |
                                        (lampOn(last_.led2_colour, last_.led2_rate) ? 0x02u : 0u));
  }
  ++steps_;
  if (!has_state_ || last_.halt != 0u) {
    return cmd; // no pose yet, or the plant is halted: zero drive
  }
  switch (mode_) {
  case MODE_TRAJECTORY:
    cmd.steer_deg = static_cast<float>(p_.trajectory_steer_deg);
    cmd.throttle_frac = static_cast<float>(p_.trajectory_throttle_frac);
    break;
  case MODE_WAYPOINT: {
    const rover_controller::GuidanceInput in{
        static_cast<double>(last_.north_m), static_cast<double>(last_.east_m),
        static_cast<double>(last_.heading_deg), static_cast<double>(last_.speed_m_s),
        static_cast<double>(last_.max_speed_m_s)};
    const rover_controller::GuidanceOutput g =
        rover_controller::waypointGuidance(p_.guidance, leg_, in);
    cmd.steer_deg = static_cast<float>(g.steer_deg);
    cmd.throttle_frac = static_cast<float>(g.throttle_frac);
    cmd.cross_track_m = static_cast<float>(g.cross_track_m);
    cmd.distance_m = static_cast<float>(g.distance_m);
    cmd.arrived = leg_.arrived;
    break;
  }
  default:
    break;
  }
  return cmd;
}

} // namespace rover_board
} // namespace appsim
