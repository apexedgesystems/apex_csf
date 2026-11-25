#ifndef APEX_SIM_ELECTRONICS_DEVICES_NONLINEAR_UTST_STAMP_PROBE_HPP
#define APEX_SIM_ELECTRONICS_DEVICES_NONLINEAR_UTST_STAMP_PROBE_HPP
/**
 * @file StampProbe.hpp
 * @brief Test support: the terminal currents a stamped device carries at its
 *        own linearization point.
 *
 * A stamp replaces a nonlinear device with its linearization at an operating
 * point. With every terminal held at that operating point by an ideal voltage
 * source, the linearized device must draw exactly the currents the model
 * reports there. The probe builds that circuit: one net per terminal, one
 * source per net to ground, the device stamp in between. The current each
 * source delivers is the current the stamped device takes at that terminal.
 *
 * A wrong sign on the equivalent current source, a transconductance written
 * as a symmetric conductance, or a missing Jacobian entry each change at
 * least one terminal current, so comparing all of them against the model's
 * currents checks the whole stamp.
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace sim::electronics::devices::nonlinear::test {

/// Relative tolerance on a terminal current.
constexpr double CURRENT_REL_TOL = 1e-9;

/// Absolute floor on a terminal current (A), below any modeled leakage.
constexpr double CURRENT_ABS_TOL = 1e-20;

/// Net of terminal `index`; net 0 is ground and carries no terminal.
[[nodiscard]] constexpr algorithms::mna::NetID terminalNet(std::size_t index) noexcept {
  return static_cast<algorithms::mna::NetID>(index + 1);
}

/// Outcome of holding a stamped device at its linearization point.
struct PinnedStamp {
  bool solved = false;      ///< The pinned system solved.
  std::vector<double> into; ///< Current into each terminal (A), in terminal order.
};

/**
 * @brief Stamp a device and hold each terminal at the given voltage.
 * @param volts Terminal voltages (V); terminal k sits on terminalNet(k).
 * @param stampFn Callable taking the MnaSystem to stamp the device into.
 * @return Whether the system solved, and the current into each terminal.
 */
template <typename StampFn>
[[nodiscard]] PinnedStamp pinTerminals(const std::vector<double>& volts, StampFn&& stampFn) {
  algorithms::mna::MnaSystem mna(volts.size() + 1);
  std::forward<StampFn>(stampFn)(mna);
  for (std::size_t k = 0; k < volts.size(); ++k) {
    mna.addVoltageSource(terminalNet(k), 0, volts[k]);
  }

  const auto RESULT = mna.solve();
  PinnedStamp pinned;
  pinned.solved = RESULT.success;
  if (RESULT.success) {
    // A source's branch current runs from its positive net into the source,
    // so the device takes the opposite.
    for (std::size_t k = 0; k < volts.size(); ++k) {
      pinned.into.push_back(-RESULT.branchCurrents[k]);
    }
  }
  return pinned;
}

/// Expect a terminal current to match the model's, to CURRENT_REL_TOL.
inline void expectCurrent(double actual, double expected, const char* what) {
  EXPECT_NEAR(actual, expected, CURRENT_REL_TOL * std::abs(expected) + CURRENT_ABS_TOL) << what;
}

} // namespace sim::electronics::devices::nonlinear::test

#endif // APEX_SIM_ELECTRONICS_DEVICES_NONLINEAR_UTST_STAMP_PROBE_HPP
