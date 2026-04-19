#ifndef APEX_SIM_ELECTRONICS_NONLINEAR_NONLINEARDEVICE_HPP
#define APEX_SIM_ELECTRONICS_NONLINEAR_NONLINEARDEVICE_HPP
/**
 * @file NonlinearDevice.hpp
 * @brief Abstract interface for nonlinear circuit elements.
 *
 * Defines the contract for devices that require Newton-Raphson iteration:
 * diodes, transistors, nonlinear resistors, etc. Each device provides its
 * current I(V) and conductance dI/dV at a given operating point.
 *
 * The Newton-Raphson solver linearizes the device around the current voltage,
 * stamps the linearized model into the MNA system, solves for voltage update,
 * and repeats until convergence.
 *
 * RT-safety: Device evaluation can be RT-safe (no allocations).
 * CUDA-ready: Device evaluation is embarrassingly parallel across devices.
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace sim::electronics::nonlinear {

using mna::MnaSystem;
using mna::NetID;

/* ----------------------------- NonlinearDevice ----------------------------- */

/**
 * @brief Abstract interface for nonlinear two-terminal devices.
 *
 * Represents a device with nonlinear I-V characteristic: I = f(V).
 * The device must provide current and conductance for Newton-Raphson:
 * - current(): I(V) at given terminal voltage
 * - conductance(): dI/dV at given terminal voltage (small-signal g)
 *
 * Devices are responsible for their own model parameters and state.
 *
 * Example devices: Diode, Resistor with temperature coefficient, Varistor.
 */
class NonlinearDevice {
public:
  virtual ~NonlinearDevice() = default;

  /**
   * @brief Get positive terminal net ID.
   * @return Positive terminal net.
   * @note RT-safe: no allocations.
   */
  [[nodiscard]] virtual NetID posNet() const noexcept = 0;

  /**
   * @brief Get negative terminal net ID.
   * @return Negative terminal net.
   * @note RT-safe: no allocations.
   */
  [[nodiscard]] virtual NetID negNet() const noexcept = 0;

  /**
   * @brief Evaluate device current at given terminal voltage.
   *
   * @param vTerminal Voltage across device (Vpos - Vneg).
   * @return Current flowing from pos to neg terminal.
   * @note RT-safe: no allocations, pure function evaluation.
   */
  [[nodiscard]] virtual double current(double vTerminal) const noexcept = 0;

  /**
   * @brief Evaluate device conductance (dI/dV) at given terminal voltage.
   *
   * Small-signal conductance for Newton-Raphson linearization.
   *
   * @param vTerminal Voltage across device (Vpos - Vneg).
   * @return Conductance dI/dV in Siemens.
   * @note RT-safe: no allocations, pure function evaluation.
   */
  [[nodiscard]] virtual double conductance(double vTerminal) const noexcept = 0;

  /**
   * @brief Stamp linearized model into MNA system.
   *
   * Linearizes device around current operating point vTerminal:
   *   I_linear = G*V + I_eq
   * where:
   *   G = dI/dV (conductance)
   *   I_eq = I(V) - G*V (equivalent current source)
   *
   * Stamps conductance and current source into MNA for Newton-Raphson iteration.
   *
   * @param mna MNA system to stamp into.
   * @param vTerminal Current operating point voltage.
   * @note RT-safe: no allocations, only stamping operations.
   */
  void stampLinearized(MnaSystem& mna, double vTerminal) const {
    double g = conductance(vTerminal);
    double i = current(vTerminal);
    double iEq = i - g * vTerminal; // Norton equivalent current source

    mna.addConductance(posNet(), negNet(), g);
    mna.addCurrent(posNet(), negNet(), iEq);
  }

  /**
   * @brief Update device state after convergence (optional).
   *
   * Called after Newton-Raphson converges. Allows device to store
   * operating point for next time step (e.g., charge storage in BJT).
   *
   * Default: no-op (stateless devices).
   *
   * @param vTerminal Converged terminal voltage.
   * @note RT-safe: no allocations.
   */
  virtual void updateState(double vTerminal) {
    (void)vTerminal; // Unused by default
  }
};

/* ----------------------------- NonlinearDeviceSet ----------------------------- */

/**
 * @brief Collection of nonlinear devices for a circuit.
 *
 * Manages all nonlinear devices and provides bulk operations for
 * Newton-Raphson iteration.
 *
 * Usage from components (Layer 2):
 * ```cpp
 * NonlinearDeviceSet devices;
 * devices.addDevice(std::make_unique<DiodeModel>(anode, cathode, Is, Vt));
 * devices.addDevice(std::make_unique<MosfetModel>(d, g, s, b, params));
 *
 * // In Newton-Raphson iteration:
 * devices.stampAllLinearized(mna, nodeVoltages);
 * ```
 */
class NonlinearDeviceSet {
private:
  std::vector<std::unique_ptr<NonlinearDevice>> devices_;

public:
  /**
   * @brief Add a nonlinear device to the set.
   *
   * @param device Unique pointer to device (takes ownership).
   * @return Index of added device.
   * @note NOT RT-safe: resizes internal vector.
   */
  std::size_t addDevice(std::unique_ptr<NonlinearDevice> device) {
    devices_.push_back(std::move(device));
    return devices_.size() - 1;
  }

  /**
   * @brief Stamp all devices' linearized models into MNA system.
   *
   * @param mna MNA system to stamp into.
   * @param nodeVoltages Current node voltages (for vTerminal calculation).
   * @note RT-safe: no allocations, iterates pre-sized vector.
   */
  void stampAllLinearized(MnaSystem& mna, const std::vector<double>& nodeVoltages) const {
    for (const auto& device : devices_) {
      double vPos = (device->posNet() < nodeVoltages.size()) ? nodeVoltages[device->posNet()] : 0.0;
      double vNeg = (device->negNet() < nodeVoltages.size()) ? nodeVoltages[device->negNet()] : 0.0;
      double vTerminal = vPos - vNeg;

      device->stampLinearized(mna, vTerminal);
    }
  }

  /**
   * @brief Update all device states after convergence.
   *
   * @param nodeVoltages Converged node voltages.
   * @note RT-safe: no allocations.
   */
  void updateAllStates(const std::vector<double>& nodeVoltages) {
    for (auto& device : devices_) {
      double vPos = (device->posNet() < nodeVoltages.size()) ? nodeVoltages[device->posNet()] : 0.0;
      double vNeg = (device->negNet() < nodeVoltages.size()) ? nodeVoltages[device->negNet()] : 0.0;
      double vTerminal = vPos - vNeg;

      device->updateState(vTerminal);
    }
  }

  /**
   * @brief Get number of devices.
   * @return Device count.
   */
  [[nodiscard]] std::size_t deviceCount() const noexcept { return devices_.size(); }

  /**
   * @brief Alias for deviceCount() (STL container compatibility).
   * @return Device count.
   */
  [[nodiscard]] std::size_t size() const noexcept { return devices_.size(); }

  /**
   * @brief Clear all devices from set.
   * @note NOT RT-safe: releases memory.
   */
  void clear() noexcept { devices_.clear(); }

  /**
   * @brief Access device by index.
   * @param idx Device index.
   * @return Reference to device.
   */
  [[nodiscard]] const NonlinearDevice& device(std::size_t idx) const { return *devices_.at(idx); }

  /**
   * @brief Access device by index (mutable).
   * @param idx Device index.
   * @return Reference to device.
   */
  [[nodiscard]] NonlinearDevice& device(std::size_t idx) { return *devices_.at(idx); }

  /**
   * @brief Access all devices.
   * @return Reference to internal device vector.
   */
  [[nodiscard]] const std::vector<std::unique_ptr<NonlinearDevice>>& devices() const noexcept {
    return devices_;
  }
};

} // namespace sim::electronics::nonlinear

#endif // APEX_SIM_ELECTRONICS_NONLINEAR_NONLINEARDEVICE_HPP
