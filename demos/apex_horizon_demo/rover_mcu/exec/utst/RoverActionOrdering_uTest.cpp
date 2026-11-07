/**
 * @file RoverActionOrdering_uTest.cpp
 * @brief The rover's action phase against a plant step still in flight.
 *
 * The demo's action engine shares its data with the scheduled tasks: its
 * watchpoints read the plant's frame bytes and its data-writes mutate the
 * plant's command state, including the halt. Both run on the executive
 * thread while the plant runs on a pool worker, so the two only stay
 * coherent if the tasks' ownership of that data ends before the action
 * phase begins.
 *
 * These tests stand in for the executive's loop with the real
 * GroundVehicle and the real multi-threaded scheduler, holding the plant
 * step in progress so the overlap is deterministic rather than a race the
 * suite happens to lose.
 *
 * Coverage:
 *  - A plant step in flight keeps the action phase out: the completion
 *    wait reports the tick as incomplete, which is the executive's signal
 *    to skip the phase for that cycle.
 *  - Once the wait succeeds, the frame the action phase reads is the one
 *    the completed step produced, not a half-written one.
 *  - A halt applied after the wait is the halt the next step sees.
 *  - A plant step that overruns its period keeps the action phase out on
 *    the following cycles too, including a cycle that dispatched nothing,
 *    until the step completes.
 */

#include "demos/apex_horizon_demo/ground_vehicle/inc/GroundVehicle.hpp"
#include "demos/apex_horizon_demo/ground_vehicle/inc/GroundVehicleCommand.hpp"

#include "src/sim/environment/celestial_body/inc/CelestialBody.hpp"
#include "src/sim/environment/factory/inc/Body.hpp"
#include "src/sim/environment/factory/inc/EnvironmentFidelity.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerMultiThread.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using apex::concurrency::DelegateU8;
using appsim::ground_vehicle::GroundVehicle;
using appsim::ground_vehicle::RoverOpcode;
using sim::environment::AtmosphereFidelity;
using sim::environment::Body;
using sim::environment::GravityFidelity;
using sim::environment::TerrainFidelity;
using sim::environment::celestial_body::CelestialBody;
using sim::environment::celestial_body::CelestialBodyTunables;
using system_core::schedulable::SchedulableTask;
using system_core::scheduler::SchedulerMultiThread;
using system_core::scheduler::Status;
using system_core::scheduler::TaskConfig;

namespace {

constexpr std::uint64_t ONE_SECOND_NS = 1000000000ULL;
constexpr std::uint64_t TEN_MS_NS = 10000000ULL;

CelestialBodyTunables analyticEarth() {
  CelestialBodyTunables t{};
  t.body = Body::EARTH;
  t.gravity_fidelity = GravityFidelity::J2;
  t.terrain_fidelity = TerrainFidelity::ELLIPSOID;
  t.atmosphere_fidelity = AtmosphereFidelity::EXPONENTIAL;
  return t;
}

void configureRover(GroundVehicle& rover) {
  auto& p = rover.tunables().get();
  p.step_hz = 100u;
  p.max_speed_m_s = 8.0;
  p.throttle_default = 0.5;
  p.steer_rate_deg_s = 0.0;
  p.init_lat_deg = 39.5;
  p.init_lon_deg = -105.5;
  p.init_heading_deg = 90.0;
  p.max_slope_deg = 25.0;
  p.lidar_n_rays = 4u;
  p.lidar_max_range_m = 100.0;
  p.lidar_fov_deg = 90.0;
  p.lidar_step_m = 5.0;
}

std::filesystem::path testLogDir() {
  const auto DIR = std::filesystem::temp_directory_path() /
                   ("rover_action_ordering_utest_" + std::to_string(::getpid()));
  std::filesystem::create_directories(DIR);
  return DIR;
}

/// The plant's step as a scheduled task, with a gate in front of it so the
/// overlap with the action phase is deterministic. `release` false holds the
/// worker inside the task with the step not yet taken.
struct GatedPlantStep {
  GroundVehicle* rover{nullptr};
  std::atomic<bool> release{true};
  std::atomic<bool> entered{false};
  std::atomic<std::uint32_t> completed{0};

  static std::uint8_t run(void* self) noexcept {
    auto* g = static_cast<GatedPlantStep*>(self);
    g->entered.store(true, std::memory_order_release);
    while (!g->release.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const std::uint8_t RC = g->rover->vehicleStep();
    g->completed.fetch_add(1, std::memory_order_acq_rel);
    return RC;
  }
};

bool awaitEntry(const GatedPlantStep& step) {
  const auto START = std::chrono::steady_clock::now();
  while (!step.entered.load(std::memory_order_acquire)) {
    if (std::chrono::steady_clock::now() - START > std::chrono::seconds(3)) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

/// Plant, body and scheduler wired the way the demo wires them: one pool
/// with one worker, the plant step as its task.
class RoverComposition {
public:
  RoverComposition() : body_(), rover_(), sched_(100, testLogDir()) {
    body_.tunables().get() = analyticEarth();
    EXPECT_EQ(body_.init(), 0u);
    configureRover(rover_);
    rover_.setBody(&body_);
    EXPECT_EQ(rover_.init(), 0u);

    gate_.rover = &rover_;
    EXPECT_EQ(sched_.init(), 0);
    EXPECT_EQ(sched_.addTask(task_, TaskConfig(100, 1, 0)), Status::SUCCESS);
  }

  ~RoverComposition() {
    gate_.release.store(true, std::memory_order_release);
    sched_.shutdown();
  }

  void dispatchTick(std::uint16_t tick) noexcept {
    gate_.entered.store(false, std::memory_order_release);
    (void)sched_.executeTasksOnTickMulti(tick);
  }

  /// The demo's overrun policy: a step still running is skipped, not
  /// queued behind itself.
  void skipOnBusy() noexcept { sched_.setSkipOnBusy(true); }

  GroundVehicle& rover() noexcept { return rover_; }
  SchedulerMultiThread& sched() noexcept { return sched_; }
  GatedPlantStep& gate() noexcept { return gate_; }

private:
  CelestialBody body_;
  GroundVehicle rover_;
  GatedPlantStep gate_{};
  DelegateU8 del_{&GatedPlantStep::run, &gate_};
  SchedulableTask task_{del_, "vehicleStep"};
  SchedulerMultiThread sched_;
};

} // namespace

/**
 * @test A plant step in flight reports the tick incomplete, keeping the
 *       action phase out until the step has finished.
 */
TEST(RoverActionOrderingTest, PlantStepInFlightKeepsActionPhaseOut) {
  RoverComposition comp;
  comp.gate().release.store(false, std::memory_order_release);
  comp.dispatchTick(0);
  ASSERT_TRUE(awaitEntry(comp.gate()));

  // The executive's boundary: this tick's task still owns the plant.
  EXPECT_FALSE(comp.sched().waitForInFlightTasks(TEN_MS_NS));
  EXPECT_EQ(comp.gate().completed.load(std::memory_order_acquire), 0u);

  comp.gate().release.store(true, std::memory_order_release);
  EXPECT_TRUE(comp.sched().waitForInFlightTasks(ONE_SECOND_NS));
  EXPECT_EQ(comp.gate().completed.load(std::memory_order_acquire), 1u);
}

/**
 * @test The frame the action phase reads is the completed step's frame.
 */
TEST(RoverActionOrderingTest, FrameReadAfterWaitIsTheCompletedStepsFrame) {
  RoverComposition comp;

  // Free-running ticks: after each completion wait, the frame's step count
  // must equal the number of steps actually taken. A read that overlapped
  // a step would see the previous count with the step in progress.
  for (std::uint16_t tick = 0; tick < 10; ++tick) {
    comp.dispatchTick(tick);
    ASSERT_TRUE(comp.sched().waitForInFlightTasks(ONE_SECOND_NS));

    const std::uint32_t TAKEN = comp.gate().completed.load(std::memory_order_acquire);
    ASSERT_NE(comp.rover().frameBytes(), nullptr);
    EXPECT_EQ(comp.rover().vehicleState().tick_count, TAKEN);
  }
}

/**
 * @test A halt applied after the wait is the halt the next step sees.
 */
TEST(RoverActionOrderingTest, HaltAppliedAfterWaitIsSeenByTheNextStep) {
  RoverComposition comp;

  // Two steps under default autonomy so the plant is moving.
  for (std::uint16_t tick = 0; tick < 2; ++tick) {
    comp.dispatchTick(tick);
    ASSERT_TRUE(comp.sched().waitForInFlightTasks(ONE_SECOND_NS));
  }
  ASSERT_GT(comp.rover().telemetry().speed_m_s, 0.0);

  // Action phase: apply the halt with no task in flight.
  static const std::uint8_t NO_PAYLOAD[1] = {0};
  apex::compat::rospan<std::uint8_t> payload(NO_PAYLOAD, 0);
  std::vector<std::uint8_t> response;
  EXPECT_EQ(
      comp.rover().handleCommand(static_cast<std::uint16_t>(RoverOpcode::HALT), payload, response),
      0u);

  const double SPEED_AT_HALT = comp.rover().telemetry().speed_m_s;
  comp.dispatchTick(2);
  ASSERT_TRUE(comp.sched().waitForInFlightTasks(ONE_SECOND_NS));

  // The halted plant coasts: the step after the command is already
  // braking, which is only true if the command landed before it.
  EXPECT_LT(comp.rover().telemetry().speed_m_s, SPEED_AT_HALT);
}

/**
 * @test An overrunning plant step keeps the action phase out on later
 *       cycles, including one that dispatched nothing, until it finishes.
 */
TEST(RoverActionOrderingTest, OverrunningStepKeepsActionPhaseOutOnLaterCycles) {
  RoverComposition comp;
  comp.skipOnBusy();
  comp.gate().release.store(false, std::memory_order_release);
  comp.dispatchTick(0);
  ASSERT_TRUE(awaitEntry(comp.gate()));

  // The cycle that dispatched it times out: the phase is skipped.
  EXPECT_FALSE(comp.sched().waitForInFlightTasks(TEN_MS_NS));

  // The next cycles find the step still running. With skip-on-busy the
  // scheduler dispatches nothing new, which is exactly the cycle a wait
  // keyed to its own dispatches would wave through -- while the worker
  // is still inside vehicleStep() with the plant's state half written.
  for (std::uint16_t tick = 1; tick <= 3; ++tick) {
    comp.dispatchTick(tick);
    EXPECT_FALSE(comp.sched().waitForInFlightTasks(TEN_MS_NS)) << "cycle " << tick;
    EXPECT_EQ(comp.gate().completed.load(std::memory_order_acquire), 0u);
  }

  // Ownership ends when the step does, and only then.
  comp.gate().release.store(true, std::memory_order_release);
  EXPECT_TRUE(comp.sched().waitForInFlightTasks(ONE_SECOND_NS));
  EXPECT_EQ(comp.gate().completed.load(std::memory_order_acquire), 1u);
  EXPECT_EQ(comp.rover().vehicleState().tick_count, 1u);
}
