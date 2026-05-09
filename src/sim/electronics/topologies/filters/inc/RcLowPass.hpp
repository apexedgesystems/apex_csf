#ifndef APEX_RCLOWPASS_HPP
#define APEX_RCLOWPASS_HPP
/**
 * @file RcLowPass.hpp
 * @brief First-order RC low-pass filter circuit model.
 *
 * Models a passive RC low-pass filter:
 *
 *     IN ---[R]---+--- OUT
 *                 |
 *                [C]
 *                 |
 *                GND
 *
 * Transfer function:
 *   H(s) = 1 / (1 + sRC)
 *   |H(jomega)| = 1 / sqrt(1 + (omegaRC)^2)
 *   Cutoff frequency: f_c = 1 / (2pi*R*C)
 *
 * Time constant: tau = R*C
 *
 * For a step input of amplitude V_in, the output rises as:
 *   V_out(t) = V_in * (1 - exp(-t/tau))
 *
 * Validated against analytical solution at multiple cutoff frequencies.
 *
 * @note RT-safe after build().
 */

#include "src/sim/electronics/circuit/inc/Circuit.hpp"
#include "src/sim/electronics/devices/linear/inc/ResistorModel.hpp"

#include <cmath>

namespace sim::electronics::topologies::filters {

using devices::linear::ResistorModel;
using algorithms::mna::MnaSystem;
using algorithms::mna::NetID;

/* ----------------------------- RcLowPass ----------------------------- */

/**
 * @brief First-order RC low-pass filter.
 *
 * Usage:
 * @code
 * double cutoffHz = 1000.0;
 * double R = 1e3;        // 1k ohm
 * double C = 1.0 / (2 * M_PI * cutoffHz * R); // ~159 nF
 *
 * RcLowPass filter(R, C);
 * filter.build();
 * filter.setInputVoltage(5.0);
 * // Run transient simulation...
 * @endcode
 */
struct RcLowPass {

  /* ----------------------------- Construction ----------------------------- */

  /**
   * @brief Construct an RC low-pass filter.
   * @param resistanceOhms Resistor value in ohms.
   * @param capacitanceFarads Capacitor value in farads.
   */
  RcLowPass(double resistanceOhms, double capacitanceFarads) noexcept
      : R_(resistanceOhms), C_(capacitanceFarads) {
    inNet_ = circuit_.addNet("IN").id;
    outNet_ = circuit_.addNet("OUT").id;
    circuit_.addStamp([R = R_, in = inNet_, out = outNet_, this](MnaSystem& mna, double,
                                                                 const std::vector<double>&) {
      // Resistor between IN and OUT
      mna.addConductance(in, out, 1.0 / R);
      // Voltage source on IN
      mna.addVoltageSource(in, circuit::Circuit::ground(), inputV_);
    });
    circuit_.addCapacitor(outNet_, circuit::Circuit::ground(), C_);
  }

  /* ----------------------------- API ----------------------------- */

  /// Build the underlying TransientSolver.
  /// @note NOT RT-safe.
  void build() { circuit_.build(); }

  /// Set the input voltage (DC for now; PWL/sine inputs in future).
  /// @note RT-safe.
  void setInputVoltage(double v) noexcept { inputV_ = v; }

  /// Get the cutoff frequency in Hz.
  /// @note RT-safe.
  [[nodiscard]] double cutoffHz() const noexcept { return 1.0 / (2.0 * M_PI * R_ * C_); }

  /// Get the time constant in seconds.
  /// @note RT-safe.
  [[nodiscard]] double tau() const noexcept { return R_ * C_; }

  /// Underlying Circuit (for advanced configuration).
  /// @note RT-safe.
  [[nodiscard]] circuit::Circuit& circuit() noexcept { return circuit_; }

  /// Net IDs for probing.
  /// @note RT-safe.
  [[nodiscard]] NetID inNet() const noexcept { return inNet_; }
  /// @note RT-safe.
  [[nodiscard]] NetID outNet() const noexcept { return outNet_; }

  /* ----------------------------- Analytical Reference ----------------------------- */

  /**
   * @brief Compute analytical step response at time t.
   * @param vIn Step input voltage (volts).
   * @param t Time after step (seconds).
   * @return V_out(t) = V_in * (1 - exp(-t/tau))
   *
   * Useful for validating the simulation against known analytical solution.
   * @note RT-safe.
   */
  [[nodiscard]] double analyticalStepResponse(double vIn, double t) const noexcept {
    return vIn * (1.0 - std::exp(-t / tau()));
  }

  /**
   * @brief Compute analytical magnitude response at frequency f.
   * @param f Frequency in Hz.
   * @return |H(jomega)| = 1 / sqrt(1 + (omegatau)^2)
   * @note RT-safe.
   */
  [[nodiscard]] double analyticalMagnitudeResponse(double f) const noexcept {
    const double OMEGA = 2.0 * M_PI * f;
    const double OMEGA_TAU = OMEGA * tau();
    return 1.0 / std::sqrt(1.0 + OMEGA_TAU * OMEGA_TAU);
  }

private:
  double R_;
  double C_;
  double inputV_ = 0.0;
  circuit::Circuit circuit_;
  NetID inNet_ = 0;
  NetID outNet_ = 0;
};

} // namespace sim::electronics::topologies::filters

#endif // APEX_RCLOWPASS_HPP
