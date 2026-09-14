#ifndef APEX_HORIZON_DEMO_ROVER_BOARD_CONTROLLER_HPP
#define APEX_HORIZON_DEMO_ROVER_BOARD_CONTROLLER_HPP
/**
 * @file RoverBoardController.hpp
 * @brief The rover controller as it runs on the board.
 *
 * Wraps the shared guidance law (RoverGuidance.hpp) in the little the
 * board owns: the arrival latch for the current leg, zero drive under a
 * halt, and the lamp strobe for its own LEDs. The mode and the leg come
 * resolved in every frame, so a board reset loses nothing. No HAL, no allocation, no logging, so
 * the same class links into the firmware and into a host test.
 */

#include "demos/apex_horizon_demo/rover_controller/inc/RoverGuidance.hpp"
#include "demos/apex_horizon_demo/rover_mcu/board/inc/RoverBoardProtocol.hpp"

#include <stdint.h>

namespace appsim {
namespace rover_board {

/* ----------------------------- BoardTunables ----------------------------- */

struct BoardTunables {
  rover_controller::GuidanceTunables guidance{};
  double trajectory_steer_deg{5.0};
  double trajectory_throttle_frac{0.6};
  uint16_t step_hz{20}; ///< The rate step() is called at (the link rate): strobe timing.
};

/* ----------------------------- RoverBoardController ----------------------------- */

class RoverBoardController {
public:
  RoverBoardController() noexcept = default;

  void setTunables(const BoardTunables& t) noexcept { p_ = t; }
  [[nodiscard]] const BoardTunables& tunables() const noexcept { return p_; }

  /**
   * @brief Take the host's latest state; a new leg resets the arrival latch.
   * @note RT-safe: O(1).
   */
  void updateState(const BoardState& s) noexcept;

  /**
   * @brief One controller step on the last state; returns the command.
   * @note RT-safe: O(1).
   */
  BoardCommand step() noexcept;

  [[nodiscard]] uint32_t stepCount() const noexcept { return steps_; }
  [[nodiscard]] uint8_t mode() const noexcept { return mode_; }
  [[nodiscard]] bool hasState() const noexcept { return has_state_; }
  [[nodiscard]] const rover_controller::GuidanceLeg& leg() const noexcept { return leg_; }
  [[nodiscard]] uint8_t lastLamp1Colour() const noexcept { return last_.led1_colour; }

private:
  /// Lamp on/off for a colour + rate code at this step (rate codes as the frame's).
  [[nodiscard]] bool lampOn(uint8_t colour, uint8_t rate) const noexcept;

  BoardTunables p_{};
  BoardState last_{};
  rover_controller::GuidanceLeg leg_{};
  uint8_t mode_{MODE_HOLD};
  uint16_t leg_seq_{0};
  uint32_t steps_{0};
  bool has_state_{false};
};

} // namespace rover_board
} // namespace appsim

#endif // APEX_HORIZON_DEMO_ROVER_BOARD_CONTROLLER_HPP
