#ifndef APEX_HORIZON_DEMO_ROVER_MCU_EXECUTIVE_HPP
#define APEX_HORIZON_DEMO_ROVER_MCU_EXECUTIVE_HPP

/**
 * @file RoverMcuExecutive.hpp
 * @brief Executive for the rover_mcu demo.
 *
 * Composes the Earth world, the rover, and its controller:
 *   - One CelestialBody (Earth) bound to the shared world bundle, so
 *     the rover drives the terrain tile.
 *   - The GroundVehicle stepping at the 100 Hz fundamental and the
 *     RoverController at 20 Hz, wired through the drive-command seam
 *     (the controller writes the block the plant consumes).
 *   - The RoverBoardLink to the NUCLEO: with the link enabled the
 *     board computes the drive, the controller forwards its command
 *     and runs the host law as a shadow; the frame's board bytes carry
 *     the link state, the board's load and its tick.
 *   - The RoverLidarModel: each plant sweep goes out as a LIDAR_SCAN on
 *     a USB-serial adapter wired to the board's sensor UART; the board
 *     reports what it saw with its command, and the frame's lidar bytes
 *     carry that picture.
 *   - The action engine's sequence catalog and safety watchpoints
 *     come from the TPRM set (no C++ here).
 *   - One ShmRingBridge publishing the rover's 256-byte ROVR/2 OUTPUT
 *     to /horizon_rover every tick and draining the reverse ring into
 *     the command bus. The bridge idles if no consumer attaches.
 *   - One TelemetryManager pushing the rover's and the controller's
 *     OUTPUT blocks and the link's and the lidar model's STATE blocks
 *     (those two drivers register no OUTPUT) over the ops interface at
 *     the rates its TPRM table sets, so a console receives them without
 *     polling.
 *
 * The executive's C++ specifies cross-component wiring only; all
 * configuration values come from the TPRM set.
 */

#include "demos/apex_horizon_demo/ground_vehicle/inc/GroundVehicle.hpp"
#include "demos/apex_horizon_demo/rover_controller/inc/RoverController.hpp"
#include "demos/apex_horizon_demo/rover_mcu/board_link/inc/RoverBoardLink.hpp"
#include "demos/apex_horizon_demo/rover_mcu/lidar_model/inc/RoverLidarModel.hpp"
#include "src/sim/environment/celestial_body/inc/CelestialBody.hpp"
#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/support/shm_ring_bridge/inc/ShmRingBridge.hpp"
#include "src/system/core/support/telemetry_manager/inc/TelemetryManager.hpp"

namespace appsim {
namespace rover_mcu {

class RoverMcuExecutive : public executive::ApexExecutive {
public:
  using ApexExecutive::ApexExecutive;
  ~RoverMcuExecutive() override = default;

  [[nodiscard]] const char* label() const noexcept override { return "ROVER_MCU_EXEC"; }

protected:
  /// Wire the rover's body, the controller's plant and seam, and the
  /// bridge resolver, then register every component so the framework
  /// runs loadTprm + init. Refuses the boot if the world is not bound.
  [[nodiscard]] bool registerComponents() noexcept override;

  /// Post-init smoke check: the rover's boot pose on the grid and the
  /// terrain under it, logged once before the scheduler takes over, and
  /// the start of the boot sequence.
  void configureComponents() noexcept override;

private:
  /// Start the armed ATS the sequence bank packs, so the demo comes up
  /// running. Which sequence that is lives in the ATS file and the
  /// manifest row that packs it; nothing here names one. No-op, logged,
  /// when the bank packs none.
  void startBootSequence() noexcept;

  sim::environment::celestial_body::CelestialBody earth_;
  appsim::ground_vehicle::GroundVehicle rover_;
  appsim::rover_controller::RoverController controller_;
  appsim::rover_board::RoverBoardLink link_;
  appsim::rover_board::RoverLidarModel lidar_;
  system_core::support::ShmRingBridge bridge_;
  system_core::support::TelemetryManager telemetry_;
};

} // namespace rover_mcu
} // namespace appsim

#endif // APEX_HORIZON_DEMO_ROVER_MCU_EXECUTIVE_HPP
