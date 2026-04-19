#ifndef APEX_SIM_ELECTRONICS_ALGORITHMS_SPICE_NGSPICE_NGSPICE_WRAPPER_HPP
#define APEX_SIM_ELECTRONICS_ALGORITHMS_SPICE_NGSPICE_NGSPICE_WRAPPER_HPP

/**
 * @file NgspiceWrapper.hpp
 * @brief Wrapper for libngspice providing golden reference SPICE simulations.
 *
 * Provides golden reference simulations for verifying device models
 * (MosfetLevel1, DiodeShockley, etc.) against industry-standard SPICE.
 *
 * @note NOT RT-safe. Calls libngspice, allocates memory dynamically.
 * @note NOT thread-safe. libngspice is not reentrant.
 */

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace sim::electronics::spice::ngspice {

/* ----------------------------- NgspiceStatus ---------------------------- */

enum class NgspiceStatus : std::uint8_t {
  OK = 0,
  ERROR_NOT_INITIALIZED,
  ERROR_NETLIST_LOAD_FAILED,
  ERROR_SIMULATION_FAILED,
  ERROR_NODE_NOT_FOUND,
  ERROR_LIBNGSPICE_NOT_AVAILABLE
};

const char* toString(NgspiceStatus status) noexcept;

/* ----------------------------- NgspiceWrapper --------------------------- */

struct NgspiceWrapper {
  /* ---------------------------- Construction ---------------------------- */

  /**
   * Initialize ngspice library and register callbacks.
   *
   * @note NOT RT-safe.
   */
  NgspiceWrapper() noexcept;

  /**
   * Clear simulation state and release resources.
   *
   * @note NOT RT-safe.
   */
  ~NgspiceWrapper() noexcept;

  // Non-copyable, non-movable (libngspice state is global)
  NgspiceWrapper(const NgspiceWrapper&) = delete;
  NgspiceWrapper& operator=(const NgspiceWrapper&) = delete;
  NgspiceWrapper(NgspiceWrapper&&) = delete;
  NgspiceWrapper& operator=(NgspiceWrapper&&) = delete;

  /* -------------------------- Netlist Loading --------------------------- */

  /**
   * Load SPICE netlist from file.
   *
   * @note NOT RT-safe.
   *
   * @param netlistPath Path to SPICE netlist file (e.g., "circuit.sp")
   * @return OK on success, error code otherwise
   */
  [[nodiscard]] NgspiceStatus loadNetlist(const std::string& netlistPath) noexcept;

  /**
   * Load SPICE netlist from string.
   *
   * @note NOT RT-safe.
   *
   * @param netlistContent SPICE netlist as string
   * @return OK on success, error code otherwise
   */
  [[nodiscard]] NgspiceStatus loadNetlistFromString(const std::string& netlistContent) noexcept;

  /* --------------------------- Simulation ------------------------------- */

  /**
   * Run DC operating point analysis.
   *
   * @note NOT RT-safe.
   *
   * @return OK on success, error code otherwise
   */
  [[nodiscard]] NgspiceStatus runDcOperatingPoint() noexcept;

  /**
   * Run transient analysis.
   *
   * @note NOT RT-safe.
   *
   * @param tstop Stop time in seconds
   * @param tstep Time step in seconds
   * @return OK on success, error code otherwise
   */
  [[nodiscard]] NgspiceStatus runTransient(double tstop, double tstep) noexcept;

  /* -------------------------- Result Extraction ------------------------- */

  /**
   * Get node voltage from last simulation.
   *
   * @note NOT RT-safe.
   *
   * @param nodeName Node name (e.g., "OUT", "VDD", "GND")
   * @param voltage Output: node voltage in volts
   * @return OK on success, ERROR_NODE_NOT_FOUND if node doesn't exist
   */
  [[nodiscard]] NgspiceStatus getNodeVoltage(const std::string& nodeName,
                                             double& voltage) const noexcept;

  /**
   * Get all node voltages from last simulation.
   *
   * @note NOT RT-safe.
   *
   * @return Map of node name -> voltage in volts
   */
  [[nodiscard]] const std::unordered_map<std::string, double>& getAllNodeVoltages() const noexcept;

  /**
   * Get transient waveform for a node.
   *
   * @note NOT RT-safe.
   *
   * @param nodeName Node name (e.g., "OUT")
   * @param times Output: time points
   * @param voltages Output: voltage values
   * @return OK on success, ERROR_NODE_NOT_FOUND if node doesn't exist
   */
  [[nodiscard]] NgspiceStatus getNodeWaveform(const std::string& nodeName,
                                              std::vector<double>& times,
                                              std::vector<double>& voltages) const noexcept;

  /* ---------------------------- Utilities ------------------------------- */

  /**
   * Check if libngspice is available at runtime.
   *
   * @note RT-safe.
   *
   * @return true if libngspice shared library is loaded
   */
  [[nodiscard]] static bool isLibngspiceAvailable() noexcept;

  /**
   * Get ngspice version string.
   *
   * @note NOT RT-safe.
   *
   * @return Version string (e.g., "ngspice-42")
   */
  [[nodiscard]] std::string getVersion() const noexcept;

  /**
   * Clear all simulation state and results.
   *
   * @note NOT RT-safe.
   */
  void clear() noexcept;

private:
  bool initialized_{false};
  std::unordered_map<std::string, double> nodeVoltages_{};
  std::unordered_map<std::string, std::vector<double>> nodeWaveforms_{};
  std::vector<double> timePoints_{};
};

} // namespace sim::electronics::spice::ngspice

#endif // APEX_SIM_ELECTRONICS_ALGORITHMS_SPICE_NGSPICE_NGSPICE_WRAPPER_HPP
