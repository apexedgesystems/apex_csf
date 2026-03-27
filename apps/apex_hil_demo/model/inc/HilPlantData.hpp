#ifndef APEX_HIL_DEMO_PLANT_DATA_HPP
#define APEX_HIL_DEMO_PLANT_DATA_HPP
/**
 * @file HilPlantData.hpp
 * @brief Data structures for HilPlantModel.
 *
 * Tunable parameters (mass, drag, controller gains) and runtime state
 * for the HIL plant simulation model. All POD, no heap.
 */

#include <cstdint>

namespace appsim {
namespace plant {

/* ----------------------------- Tunable Parameters ----------------------------- */

/**
 * @struct HilPlantTunableParams
 * @brief Runtime-adjustable plant and controller configuration.
 *
 * Size: 64 bytes (8 doubles)
 */
struct HilPlantTunableParams {
  double mass{10.0};           ///< Vehicle mass [kg].
  double dragCd{0.5};          ///< Drag coefficient.
  double dragArea{0.1};        ///< Reference area [m^2].
  double targetAlt{100.0};     ///< PD controller target altitude [m].
  double ctrlKp{2.0};          ///< Proportional gain [N/m].
  double ctrlKd{1.5};          ///< Derivative gain [N/(m/s)].
  double thrustMax{200.0};     ///< Maximum thrust magnitude [N].
  double commLossFrames{25.0}; ///< Control frames without new cmd before comm loss (0=disabled).
};

static_assert(sizeof(HilPlantTunableParams) == 64, "HilPlantTunableParams size mismatch");

/* ----------------------------- State ----------------------------- */

/**
 * @struct HilPlantState
 * @brief Internal state tracking plant simulation progress.
 *
 * Size: 40 bytes
 */
struct HilPlantState {
  double simTime{0.0};              ///< Elapsed simulation time [s].
  double lastAlt{0.0};              ///< Last computed altitude [m].
  double lastVz{0.0};               ///< Last vertical velocity [m/s].
  std::uint32_t stepCount{0};       ///< Plant steps executed.
  std::uint32_t ctrlCount{0};       ///< Control updates executed.
  std::uint32_t tlmCount{0};        ///< Telemetry reports sent.
  std::uint32_t commLossCount{0};   ///< Comm loss events detected.
  std::uint32_t staleCmdFrames{0};  ///< Consecutive control frames with no new command.
  std::uint32_t lastSeenRxCount{0}; ///< Snapshot of driver rxCount for staleness detection.
  std::uint8_t commLost{0};         ///< 1 if currently in comm loss state.
  std::uint8_t reserved[7]{};       ///< Padding for 8-byte alignment.
};

static_assert(sizeof(HilPlantState) == 56, "HilPlantState size mismatch");

} // namespace plant
} // namespace appsim

#endif // APEX_HIL_DEMO_PLANT_DATA_HPP
