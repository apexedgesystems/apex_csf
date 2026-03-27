#ifndef APEX_MATH_INTEGRATION_LEAPFROG_HPP
#define APEX_MATH_INTEGRATION_LEAPFROG_HPP
/**
 * @file Leapfrog.hpp
 * @brief Symplectic Leapfrog/Verlet integrators for Hamiltonian systems.
 *
 * Energy-conserving integrators for long-duration simulations:
 *  - Leapfrog (Stormer-Verlet): Second-order symplectic
 *  - VelocityVerlet: Velocity-explicit variant
 *
 * Use cases: Orbital mechanics, molecular dynamics, satellite attitude.
 * @note RT-safe: Zero allocation, O(1) per step.
 */

#include <cstddef>
#include <cstdint>

namespace apex {
namespace math {
namespace integration {

/* -------------------------------- Status ---------------------------------- */

enum class LeapfrogStatus : uint8_t { SUCCESS = 0, ERROR_INVALID_STEP = 1 };

/* -------------------------------- Leapfrog -------------------------------- */

/**
 * @brief Stormer-Verlet (Leapfrog) symplectic integrator.
 *
 * For systems of the form: dx/dt = v, dv/dt = a(x)
 * where acceleration depends only on position (Hamiltonian).
 *
 * Algorithm (kick-drift-kick form):
 *   v_{n+1/2} = v_n + (dt/2) * a(x_n)
 *   x_{n+1}   = x_n + dt * v_{n+1/2}
 *   v_{n+1}   = v_{n+1/2} + (dt/2) * a(x_{n+1})
 *
 * Properties:
 *  - Second-order accurate
 *  - Symplectic (energy-conserving for Hamiltonian systems)
 *  - Time-reversible
 *
 * @tparam State Position/velocity state type (must support +, *, scalar).
 *
 * Example:
 * @code
 *   // Simple harmonic oscillator: a = -x
 *   auto accel = [](const double& x) { return -x; };
 *
 *   Leapfrog<double> integrator;
 *   double x = 1.0, v = 0.0;
 *   for (int i = 0; i < 1000; ++i) {
 *     integrator.step(x, v, 0.01, accel);
 *   }
 *   // Energy is conserved: x^2 + v^2 ≈ 1.0
 * @endcode
 *
 * @note RT-safe: Zero allocation, two acceleration evaluations per step.
 */
template <typename State> class Leapfrog {
public:
  /** @brief Statistics for this integrator. */
  struct Stats {
    std::size_t accelEvals{0};
    std::size_t steps{0};
  };

  /**
   * @brief Perform one leapfrog step.
   *
   * @tparam AccelFunc Callable: State(const State& position)
   * @param x Position (modified in place).
   * @param v Velocity (modified in place).
   * @param dt Time step (must be positive).
   * @param accel Acceleration function of position only.
   * @return Status code.
   * @note RT-safe: O(1), two accel evaluations.
   */
  template <typename AccelFunc>
  uint8_t step(State& x, State& v, double dt, AccelFunc&& accel) noexcept {
    if (dt <= 0.0) {
      return static_cast<uint8_t>(LeapfrogStatus::ERROR_INVALID_STEP);
    }

    const double HALF_DT = 0.5 * dt;

    // Kick: v_{n+1/2} = v_n + (dt/2) * a(x_n)
    State a0 = accel(x);
    ++stats_.accelEvals;
    v = v + a0 * HALF_DT;

    // Drift: x_{n+1} = x_n + dt * v_{n+1/2}
    x = x + v * dt;

    // Kick: v_{n+1} = v_{n+1/2} + (dt/2) * a(x_{n+1})
    State a1 = accel(x);
    ++stats_.accelEvals;
    v = v + a1 * HALF_DT;

    ++stats_.steps;
    t_ += dt;

    return static_cast<uint8_t>(LeapfrogStatus::SUCCESS);
  }

  /** @brief Reset statistics and time. */
  void reset(double t0 = 0.0) noexcept {
    stats_ = Stats{};
    t_ = t0;
  }

  /** @brief Current time. */
  double time() const noexcept { return t_; }

  /** @brief Integration statistics. */
  const Stats& stats() const noexcept { return stats_; }

private:
  Stats stats_{};
  double t_{0.0};
};

/* ----------------------------- VelocityVerlet ----------------------------- */

/**
 * @brief Velocity Verlet integrator (equivalent to Leapfrog, different form).
 *
 * For systems: dx/dt = v, dv/dt = a(x, v, t)
 * Allows velocity-dependent forces (with reduced symplectic properties).
 *
 * Algorithm:
 *   a_n = a(x_n, v_n, t_n)
 *   x_{n+1} = x_n + v_n * dt + 0.5 * a_n * dt^2
 *   a_{n+1} = a(x_{n+1}, v_n + a_n * dt, t_{n+1})  // predictor
 *   v_{n+1} = v_n + 0.5 * (a_n + a_{n+1}) * dt
 *
 * @tparam State Position/velocity state type.
 *
 * @note RT-safe: Zero allocation, two acceleration evaluations per step.
 */
template <typename State> class VelocityVerlet {
public:
  struct Stats {
    std::size_t accelEvals{0};
    std::size_t steps{0};
  };

  /**
   * @brief Perform one Velocity Verlet step.
   *
   * @tparam AccelFunc Callable: State(const State& x, const State& v, double t)
   * @param x Position (modified in place).
   * @param v Velocity (modified in place).
   * @param dt Time step.
   * @param accel Acceleration function.
   * @return Status code.
   */
  template <typename AccelFunc>
  uint8_t step(State& x, State& v, double dt, AccelFunc&& accel) noexcept {
    if (dt <= 0.0) {
      return static_cast<uint8_t>(LeapfrogStatus::ERROR_INVALID_STEP);
    }

    const double HALF_DT = 0.5 * dt;
    const double HALF_DT_SQ = 0.5 * dt * dt;

    // Current acceleration
    State a0 = accel(x, v, t_);
    ++stats_.accelEvals;

    // Update position: x_{n+1} = x_n + v_n * dt + 0.5 * a_n * dt^2
    x = x + v * dt + a0 * HALF_DT_SQ;

    // Predicted velocity for acceleration evaluation
    State vPred = v + a0 * dt;

    // New acceleration at new position
    State a1 = accel(x, vPred, t_ + dt);
    ++stats_.accelEvals;

    // Update velocity: v_{n+1} = v_n + 0.5 * (a_n + a_{n+1}) * dt
    v = v + (a0 + a1) * HALF_DT;

    ++stats_.steps;
    t_ += dt;

    return static_cast<uint8_t>(LeapfrogStatus::SUCCESS);
  }

  void reset(double t0 = 0.0) noexcept {
    stats_ = Stats{};
    t_ = t0;
  }

  double time() const noexcept { return t_; }
  const Stats& stats() const noexcept { return stats_; }

private:
  Stats stats_{};
  double t_{0.0};
};

} // namespace integration
} // namespace math
} // namespace apex

#endif // APEX_MATH_INTEGRATION_LEAPFROG_HPP
