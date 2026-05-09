#ifndef APEX_INTEL4004GRID_HPP
#define APEX_INTEL4004GRID_HPP
/**
 * @file Intel4004Grid.hpp
 * @brief Intel 4004 transistor-level circuit assembly from SPICE netlist.
 *
 * Converts a parsed Intel4004Netlist (2,242 PMOS transistors, ~1,081 nets) into
 * a Circuit object for MNA simulation. All transistors are stamped as switch-level
 * PMOS: conducting when Vgs < -Vth, cutoff otherwise.
 *
 * External interface:
 * - Two-phase non-overlapping clock (CLK1/CLK2)
 * - Behavioral ROM drives D0-D3 with instruction bytes during M1/M2 phases
 * - 8-state machine cycle: A1-A3 (address), M1-M2 (memory read), X1-X3 (execute)
 *
 * Sparse-only: at 1,081 nets, dense MNA is impractical. Use enableSparseMode()
 * before simulation.
 *
 * @note NOT RT-safe: initialization allocates. Stamp functions are RT-safe.
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystemSparse.hpp"
#include "src/sim/electronics/algorithms/mna/inc/Types.hpp"
#include "src/sim/electronics/algorithms/transient/inc/TransientSolver.hpp"
#include "src/sim/electronics/circuit/inc/Circuit.hpp"
#include "src/sim/electronics/chips/intel4004/netlist/inc/Intel4004Netlist.hpp"
#include "src/sim/electronics/devices/composite/inc/GateConstants.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sim::electronics::chips::intel4004 {

using sim::electronics::circuit::Circuit;
using sim::electronics::circuit::CircuitNet;
using sim::electronics::algorithms::mna::MnaSystem;
using sim::electronics::algorithms::mna::NetID;
using sim::electronics::algorithms::transient::TransientState;
using sim::electronics::algorithms::transient::TransientStatus;

/* ----------------------------- ResolvedTransistor ----------------------------- */

/**
 * @brief Pre-resolved transistor with NetIDs instead of string names.
 *
 * Resolved during buildCircuit() so stamp functions avoid hash lookups
 * per time step. 24 bytes per transistor (2,242 entries = ~54 KB).
 */
struct ResolvedTransistor {
  sim::electronics::algorithms::mna::NetID drain;
  sim::electronics::algorithms::mna::NetID gate;
  sim::electronics::algorithms::mna::NetID source;
  bool isLoad;      ///< True for pull-up transistors (drain or source at VDD).
  bool isDiodeLoad; ///< True for diode-connected loads (gate==drain==VDD): always ON.
};

/* ----------------------------- Intel4004Grid ----------------------------- */

/**
 * @brief Intel 4004 transistor-level circuit assembled from SPICE netlist.
 *
 * Provides buildCircuit() to construct a Circuit from the parsed netlist,
 * enableSparseMode() for KLU sparse solving, simulate() for clock-driven
 * execution, and net readback helpers for verification.
 */
struct Intel4004Grid {

  /* ----------------------------- Constants ----------------------------- */

  static constexpr double VTH = devices::composite::DEFAULT_VTH;
  static constexpr double G_ON = 1.0 / devices::composite::DEFAULT_RDS_ON;
  static constexpr double G_OFF = 1.0 / devices::composite::DEFAULT_RDS_OFF;
  static constexpr double G_OFF_STORAGE = 1e-15; ///< Near-zero leakage for storage nodes.
  static constexpr double VDD_VOLTAGE = devices::composite::DEFAULT_VDD;
  static constexpr double THRESHOLD = VDD_VOLTAGE / 2.0;
  static constexpr double G_DRIVER = 100.0; ///< Strong drive to overwhelm Level 1 stamp loads.
  static constexpr double G_WEAK = 1.0 / devices::composite::DEFAULT_RDS_OFF;
  static constexpr double CPARA = 100e-15; ///< Parasitic node capacitance (100fF).

  /// Subthreshold conductance for transistors near threshold. In real PMOS,
  /// transistors don't turn fully OFF at Vgs = -Vth - they continue conducting
  /// with exponentially decreasing current (subthreshold/weak inversion region).
  /// This weak conductance provides crucial charge retention for dynamic nodes.
  static constexpr double G_SUBTH = 5.0e-9;   ///< Subthreshold conductance.
  static constexpr double SUBTH_MARGIN = 0.3; ///< Voltage margin around Vth.

  /// Load-to-logic conductance ratio. In the real 4004, depletion-mode pull-up
  /// loads have higher channel resistance than enhancement-mode logic transistors.
  /// Without this ratio, contested nodes sit at VDD/2 (threshold) and logic
  /// levels cannot propagate through cascaded gates. Ratio of 4:1 gives clean
  /// LOW ~1.0V when a pull-down fights a load.
  static constexpr double LOAD_RATIO = 4.0;
  static constexpr double G_LOAD = G_ON / LOAD_RATIO;

  /* ----------------------------- Tunable Parameters ----------------------------- */

  /// Configurable binary switch parameters for Monte Carlo optimization.
  /// Defaults match the static constexpr values above. Set these BEFORE
  /// calling buildCircuit() or simulate() to override defaults.
  struct BinarySwitchParams {
    double gOn = 1.0 / devices::composite::DEFAULT_RDS_ON;
    double gOff = 1.0 / devices::composite::DEFAULT_RDS_OFF;
    double gSubth = 5.0e-9;
    double gLoad = gOn / 4.0;
    double cpara = 100e-15;
    double vth = devices::composite::DEFAULT_VTH;
    double subthMargin = 0.3;
  };

  BinarySwitchParams bsParams_; ///< Tunable parameters (MC can modify these).

  /* ----------------------------- Transistor State ----------------------------- */

  std::vector<ResolvedTransistor> transistors_;
  std::unordered_map<std::string, sim::electronics::algorithms::mna::NetID> netMap_;
  sim::electronics::algorithms::mna::NetID vdd_ = 0;

  /* ----------------------------- External IO Nets ----------------------------- */

  sim::electronics::algorithms::mna::NetID clk1Net_ = 0;
  sim::electronics::algorithms::mna::NetID clk2Net_ = 0;
  std::array<sim::electronics::algorithms::mna::NetID, 4> dataBusNets_{}; ///< D0-D3.
  std::array<sim::electronics::algorithms::mna::NetID, 4> accNets_{};     ///< ACC.0-ACC.3.

  /* ----------------------------- Timing Generator Nets ----------------------------- */

  /// Machine state timing signals. Index matches machineState_ (0-7).
  /// Each signal goes LOW (PMOS active) during its corresponding state
  /// when CLK2 is active. [0]=A12, [1]=A22, [2]=A32, [3]=M12,
  /// [4]=M22, [5]=X12, [6]=X22, [7]=X32.
  std::array<sim::electronics::algorithms::mna::NetID, 8> timingNets_{};
  sim::electronics::algorithms::mna::NetID syncNet_ = 0;      ///< SYNC: HIGH during A1-A3.
  sim::electronics::algorithms::mna::NetID execCtrlNet_ = 0;  ///< ~(X31&~CLK2): execute-phase ctrl.
  sim::electronics::algorithms::mna::NetID execCtrl2Net_ = 0; ///< ~(X21&~CLK2): another exec ctrl.
  bool behavioralTiming_ = false;                 ///< Drive timing signals behaviorally.

  /// All compound timing signal NetIDs. Cached during cacheExternalNets().
  struct CompoundTimingNets {
    algorithms::mna::NetID inhX11X31Clk1 = 0; // (~INH)(X11+X31)CLK1
    algorithms::mna::NetID pocClk2A32X12 = 0; // (~POC)&CLK2&SC(A32+X12)
    algorithms::mna::NetID clk2A12M12 = 0;    // CLK2&SC(A12+M12)
    algorithms::mna::NetID m12m22Clk1 = 0;    // M12+M22+CLK1~(M11+M12)
    algorithms::mna::NetID scA22M22Clk2 = 0;  // SC(A22+M22)CLK2
    algorithms::mna::NetID scM22Clk2 = 0;     // SC&M22&CLK2
    algorithms::mna::NetID scM12Clk2 = 0;     // SC&M12&CLK2
    algorithms::mna::NetID scA12Clk2 = 0;     // SC&A12&CLK2
    algorithms::mna::NetID scA22 = 0;         // SC&A22
    algorithms::mna::NetID inhX32Clk2 = 0;    // (~INH)&X32&CLK2
    algorithms::mna::NetID pocClk2X12X32 = 0; // (~POC)CLK2(X12+X32)~INH
    algorithms::mna::NetID jmsDcBbl = 0;      // CLK2(JMS&DC&M22+BBL(M22+X12+X22))
    algorithms::mna::NetID scJinFin = 0;      // ((~SC)(JIN+FIN))CLK1(M11+X21~INH)
  } compNets_;

  /* ----------------------------- Internal State Nets ----------------------------- */

  sim::electronics::algorithms::mna::NetID cyNet_ = 0;                                ///< CY (carry flag).
  std::array<std::array<sim::electronics::algorithms::mna::NetID, 4>, 16> regNets_{}; ///< R0.0-R15.3.
  std::array<sim::electronics::algorithms::mna::NetID, 12> pcNets_{};                 ///< PC0.0-PC0.11.

  /* ----------------------------- Simulation State ----------------------------- */

  std::uint8_t machineState_ = 0;      ///< 0-7 = A1..X3.
  bool clk1High_ = false;              ///< Current CLK1 level.
  bool clk2High_ = false;              ///< Current CLK2 level.
  std::uint8_t dataBusDrive_ = 0;      ///< Nibble to drive on D0-D3 during M1/M2.
  bool dataBusDriving_ = false;        ///< True when ROM is driving the bus.
  std::size_t bytesFetched_ = 0;       ///< ROM bytes served so far.

public:
  std::uint16_t lastCapturedAddr_ = 0; ///< Address read from D0-D3 during A1-A3.
  double stepDt_ = 0.0;                ///< Timestep for current solve (set before step).

  /* ----------------------------- API ----------------------------- */

  /**
   * @brief Build a Circuit from a parsed Intel 4004 SPICE netlist.
   * @param netlist Parsed netlist with transistors and unique net names.
   * @return Configured Circuit ready for enableSparseMode() + simulation.
   *
   * Allocates ~1,081 nets, pre-resolves all 2,242 transistors to NetIDs,
   * and registers stamp functions for VDD supply, PMOS conductances, and
   * external IO (clock + data bus).
   *
   * @note NOT RT-safe: allocates.
   */
  Circuit buildCircuit(const Intel4004Netlist& netlist) {
    Circuit circuit;

    allocateNets(circuit, netlist);
    cacheExternalNets();
    resolveTransistors(netlist);
    registerStamps(circuit);

    return circuit;
  }

  /**
   * @brief Enable sparse KLU solver mode.
   * @param circuit Configured Circuit from buildCircuit().
   *
   * Must be called AFTER buildCircuit() but BEFORE simulate().
   * Registers sparse stamp callbacks and enables KLU solver.
   */
  void enableSparseMode(Circuit& circuit) {
    circuit.solver().setStatefulStampCallbackSparse(
        [this](sim::electronics::algorithms::mna::MnaSystemSparse& mna, double /*time*/,
               const std::vector<double>& prevV) {
          mna.addVoltageSource(vdd_, Circuit::ground(), VDD_VOLTAGE);
          stampTransistors(mna, prevV);
          stampExternalIO(mna);
          stampParasiticCaps(mna, prevV);
        });
    circuit.solver().setSparse(true);
  }

  /**
   * @brief Run clock-driven transient simulation.
   * @param circuit Configured Circuit from buildCircuit().
   * @param rom Program ROM bytes.
   * @param romSize Size of ROM array.
   * @param numBytes Number of ROM bytes to fetch (one per 8-state cycle).
   * @param clockPeriod Time per machine state (CLK1 + CLK2 pulse) in seconds.
   * @param stepsPerPhase Sub-steps per clock high/low phase for signal propagation.
   * @param convergenceThreshold Early termination threshold (0 = disabled).
   * @return Final sim::electronics::algorithms::transient::TransientState with node voltages.
   *
   * Drives the two-phase non-overlapping clock (CLK1/CLK2) through 8 machine
   * states per byte fetch. During M1/M2, feeds instruction nibbles on D0-D3.
   * During A1-A3, captures the address the CPU outputs on D0-D3.
   *
   * @note NOT RT-safe: allocates state/history storage.
   */
  sim::electronics::algorithms::transient::TransientState
  simulate(Circuit& circuit, const std::uint8_t* rom, std::size_t romSize,
           std::size_t numBytes = 16, double clockPeriod = 1e-6, std::size_t stepsPerPhase = 20,
           double convergenceThreshold = 0.01) {
    sim::electronics::algorithms::transient::TransientState state;
    state.resize(circuit.netCount(), 0);

    double phaseDuration = clockPeriod / 4.0; // 4 sub-phases per state
    double subStep = phaseDuration / static_cast<double>(stepsPerPhase);

    // Transient power-up initialization instead of DC solve.
    //
    // The 4004 is dynamic PMOS: its timing generator is a dynamic shift register
    // (ring counter) that needs clock transitions to bootstrap. A DC solve finds
    // a mathematically valid static equilibrium where all timing signals are HIGH
    // (ring counter stuck in all-precharged state), but this state is physically
    // unreachable -- the real chip powers up through a transient sequence where
    // depletion loads charge nodes, and the first clock edges create asymmetry
    // that starts the ring counter.
    //
    // Instead of DC, we start from all zeros (VDD applied instantly) and run
    // a few clock cycles of transient simulation. The load transistors pull
    // internal nodes toward VDD, and the alternating CLK1/CLK2 phases propagate
    // state through the dynamic shift register, initializing the timing generator.
    dataBusDriving_ = false;
    clk1High_ = true;
    clk2High_ = true;

    // Enable behavioral timing injection to bypass the timing generator
    // ring counter, which cannot bootstrap in simulation.
    behavioralTiming_ = true;

    // Convergence detection
    const bool USE_CONVERGENCE = convergenceThreshold > 0.0;
    const std::size_t NET_COUNT = circuit.netCount();
    std::vector<double> prevSubV;
    if (USE_CONVERGENCE) {
      prevSubV.resize(NET_COUNT);
    }

    // Cached LU disabled: parasitic caps + threshold transistors cause cached
    // LU divergence. Each sub-step does a fresh KLU factorize via invalidateCache().
    circuit.solver().setCachedLU(false);

    bytesFetched_ = 0;

    runByteLoop(circuit, state, rom, romSize, 0, numBytes, subStep, stepsPerPhase, USE_CONVERGENCE,
                convergenceThreshold, prevSubV);

    // Clean up
    circuit.solver().setCachedLU(false);

    return state;
  }

  /* ----------------------------- Readback ----------------------------- */

  /**
   * @brief Read accumulator value from ACC.0-ACC.3 node voltages.
   *
   * Uses PMOS-inverted readback: internal ACC nets store data in complemented
   * form (high voltage = logic 0, low voltage = logic 1).
   */
  [[nodiscard]] std::uint8_t readAccumulator(const std::vector<double>& voltages) const {
    return static_cast<std::uint8_t>(readBusPmos(voltages, accNets_.data(), 4) & 0xF);
  }

  /**
   * @brief Read data bus D0-D3 value from node voltages.
   *
   * Uses PMOS-inverted readback: the internal D0-D3 nets carry data in PMOS
   * convention (low voltage = logic 1, high voltage = logic 0).
   */
  [[nodiscard]] std::uint8_t readDataBus(const std::vector<double>& voltages) const {
    return static_cast<std::uint8_t>(readBusPmos(voltages, dataBusNets_.data(), 4) & 0xF);
  }

  /**
   * @brief Get the last captured ROM address from A1-A3 phases.
   */
  [[nodiscard]] std::uint16_t lastCapturedAddress() const noexcept { return lastCapturedAddr_; }

  /**
   * @brief Get the number of ROM bytes fetched so far.
   */
  [[nodiscard]] std::size_t bytesFetched() const noexcept { return bytesFetched_; }

  /**
   * @brief Read index register Rn (4-bit) from node voltages.
   * @param idx Register index (0-15).
   *
   * Uses PMOS-inverted readback.
   */
  [[nodiscard]] std::uint8_t readRegister(std::size_t idx,
                                          const std::vector<double>& voltages) const {
    if (idx >= 16) {
      return 0;
    }
    return static_cast<std::uint8_t>(readBusPmos(voltages, regNets_[idx].data(), 4) & 0xF);
  }

  /**
   * @brief Read carry flag from CY node voltage.
   *
   * Uses PMOS-inverted readback: high voltage = carry clear.
   */
  [[nodiscard]] bool readCarry(const std::vector<double>& voltages) const {
    return cyNet_ < voltages.size() && voltages[cyNet_] < THRESHOLD;
  }

  /**
   * @brief Read 12-bit program counter from PC0.0-PC0.11 node voltages.
   *
   * Uses PMOS-inverted readback.
   */
  [[nodiscard]] std::uint16_t readPC(const std::vector<double>& voltages) const {
    return static_cast<std::uint16_t>(readBusPmos(voltages, pcNets_.data(), 12) & 0xFFF);
  }

  /* ----------------------------- Net Readback (by name) ----------------------------- */

  /**
   * @brief Find a net's ID by name.
   * @param name SPICE net name (e.g., "VDD", "D0", "CLK1").
   * @return sim::electronics::algorithms::mna::NetID, or 0 if not found.
   */
  [[nodiscard]] sim::electronics::algorithms::mna::NetID findNet(const std::string& name) const {
    auto it = netMap_.find(name);
    return (it != netMap_.end()) ? it->second : 0;
  }

  /**
   * @brief Read voltage at a named net.
   */
  [[nodiscard]] double readNetVoltage(const std::vector<double>& voltages,
                                      const std::string& name) const {
    sim::electronics::algorithms::mna::NetID id = findNet(name);
    return (id < voltages.size()) ? voltages[id] : 0.0;
  }

  /**
   * @brief Read a named net as logic level (threshold comparison).
   */
  [[nodiscard]] bool readNetLogic(const std::vector<double>& voltages,
                                  const std::string& name) const {
    return readNetVoltage(voltages, name) > THRESHOLD;
  }

  /**
   * @brief Get the number of allocated circuit nets (including ground).
   */
  [[nodiscard]] std::size_t netCount() const noexcept { return netMap_.size() + 1; }

  /**
   * @brief Get the number of pre-resolved transistors.
   */
  [[nodiscard]] std::size_t transistorCount() const noexcept { return transistors_.size(); }

protected:
  /* ----------------------------- Net Helpers ----------------------------- */

  template <typename MnaSystemT>
  void driveNet(MnaSystemT& mna, sim::electronics::algorithms::mna::NetID netId, bool high) const {
    // Use voltage source to absolutely fix the driven node.
    // Conductance-based drives (G_DRIVER) are overwhelmed by Level 1
    // stamp loads on timing nets, causing behavioral signals to drift
    // to mid-rail during NR iteration.
    mna.addVoltageSource(netId, Circuit::ground(), high ? VDD_VOLTAGE : 0.0);
  }

  /**
   * @brief Drive data bus in PMOS convention.
   *
   * The 4004's internal D0-D3 nets use PMOS convention: logic 1 = LOW
   * voltage, logic 0 = HIGH voltage. PMOS-inverted data is driven onto
   * the bus by the ROM chip (4001).
   */
  template <typename MnaSystemT>
  void driveBus(MnaSystemT& mna, const sim::electronics::algorithms::mna::NetID* nets, std::size_t bits,
                std::uint32_t value) const {
    for (std::size_t i = 0; i < bits; ++i) {
      driveNet(mna, nets[i], ((value >> i) & 1) == 0); // PMOS: 1=LOW, 0=HIGH
    }
  }

  static std::uint32_t readBus(const std::vector<double>& v,
                               const sim::electronics::algorithms::mna::NetID* nets, std::size_t bits) {
    std::uint32_t val = 0;
    for (std::size_t i = 0; i < bits; ++i) {
      if (nets[i] < v.size() && v[nets[i]] > (devices::composite::DEFAULT_VDD / 2.0)) {
        val |= (1U << i);
      }
    }
    return val;
  }

  /**
   * @brief Read bus value with PMOS-inverted logic.
   *
   * In PMOS dynamic logic, internal nodes store data in complemented form:
   * high voltage (> VDD/2) = logic 0, low voltage (< VDD/2) = logic 1.
   */
  static std::uint32_t readBusPmos(const std::vector<double>& v,
                                   const sim::electronics::algorithms::mna::NetID* nets, std::size_t bits) {
    std::uint32_t val = 0;
    for (std::size_t i = 0; i < bits; ++i) {
      if (nets[i] < v.size() && v[nets[i]] < (devices::composite::DEFAULT_VDD / 2.0)) {
        val |= (1U << i);
      }
    }
    return val;
  }

  /* ----------------------------- Hybrid Level 1 for Storage Nodes ----------------------------- */

public:
  bool hybridLevel1_ = false; ///< When true, ACC/register transistors use Level 1.

protected:
  mutable std::unordered_set<algorithms::mna::NetID>
      storageNets_; ///< Nets that are storage nodes (ACC, registers).

  /// Build set of storage nets from accNets_ only (not registers).
  /// ACC is the register that must hold data between instruction cycles.
  /// Index registers are rewritten each cycle so don't need hold protection.
  void buildStorageNetSet() const {
    storageNets_.clear();
    for (auto n : accNets_) {
      if (n > 0)
        storageNets_.insert(n);
    }
    if (cyNet_ > 0)
      storageNets_.insert(cyNet_);
  }

  /// Check if a transistor is a PASS GATE on a storage node.
  /// True only when: (1) drain or source is a storage net (ACC, register), AND
  /// (2) gate is NOT a storage net (gate is a control/timing signal).
  /// This excludes NOR pull-downs where the storage net is the gate terminal.
public:
  bool isStoragePassGate(std::size_t idx) const {
    if (storageNets_.empty())
      buildStorageNetSet();
    const auto& t = transistors_[idx];
    bool touchesStorage = storageNets_.count(t.drain) || storageNets_.count(t.source);
    bool gateIsStorage = storageNets_.count(t.gate);
    return touchesStorage && !gateIsStorage;
  }

protected:
  /// Stamp one transistor using Level 1 Shichman-Hodges model.
  /// Used for storage node transistors in hybrid mode. Sparse MNA only
  /// (addMatrixEntry not available on dense MnaSystem).
  void stampTransistorLevel1(algorithms::mna::MnaSystemSparse& mna, const ResolvedTransistor& t,
                             const std::vector<double>& prevV) const {
    double VS = prevV[t.source], VD = prevV[t.drain], VG = prevV[t.gate];
    double VSG = VS - VG, VSD = VS - VD;

    algorithms::mna::NetID sD = t.drain, sS = t.source;
    double eVSG = VSG, eVSD = VSD;
    if (VSD < 0.0) {
      std::swap(sD, sS);
      eVSG = VD - VG;
      eVSD = VD - VS;
    }

    // Uniform model: KP=24uA/V^2 (W/L=2, process KP=12u), VTH=1.17V
    static constexpr double KP = 24e-6;
    static constexpr double VTH_L1 = 1.17;
    static constexpr double LAMBDA = 0.03;
    devices::nonlinear::MosfetLevel1Params params{.Kp = KP, .Vth = VTH_L1, .lambda = LAMBDA};

    double vsgM = std::max(eVSG, 0.0), vsdM = std::max(eVSD, 0.0);
    double id = devices::nonlinear::MosfetLevel1::current(vsgM, vsdM, params);
    double gm = devices::nonlinear::MosfetLevel1::transconductance(vsgM, vsdM, params);
    double gdsDevice = devices::nonlinear::MosfetLevel1::outputConductance(vsgM, vsdM, params);
    double gdsStamp = std::max(gdsDevice, 1e-12);
    double ieq = id - gm * eVSG - gdsDevice * eVSD;

    // Asymmetric PMOS stamp (VCCS + output conductance)
    mna.addConductance(sD, sS, gdsStamp);
    mna.addMatrixEntry(sD, t.gate, gm);
    mna.addMatrixEntry(sD, sS, -gm);
    mna.addMatrixEntry(sS, t.gate, -gm);
    mna.addMatrixEntry(sS, sS, gm);
    mna.addCurrent(sD, sS, ieq);
  }

  /* ----------------------------- Net Allocation ----------------------------- */

  void allocateNets(Circuit& circuit, const Intel4004Netlist& netlist) {
    netMap_.clear();
    netMap_.reserve(netlist.netCount());

    for (const auto& name : netlist.uniqueNets) {
      if (name == "GND") {
        netMap_[name] = Circuit::ground();
        continue;
      }

      sim::electronics::algorithms::mna::NetID id = circuit.addNet(name).id;
      netMap_[name] = id;

      if (name == "VDD") {
        vdd_ = id;
      }
    }
  }

  void cacheExternalNets() {
    clk1Net_ = findNet("CLK1");
    clk2Net_ = findNet("CLK2");
    dataBusNets_[0] = findNet("D0");
    dataBusNets_[1] = findNet("D1");
    dataBusNets_[2] = findNet("D2");
    dataBusNets_[3] = findNet("D3");
    accNets_[0] = findNet("ACC.0");
    accNets_[1] = findNet("ACC.1");
    accNets_[2] = findNet("ACC.2");
    accNets_[3] = findNet("ACC.3");

    // Carry flag
    cyNet_ = findNet("CY");

    // Timing generator signals (for behavioral bypass)
    timingNets_[0] = findNet("A12"); // State 0 = A1
    timingNets_[1] = findNet("A22"); // State 1 = A2
    timingNets_[2] = findNet("A32"); // State 2 = A3
    timingNets_[3] = findNet("M12"); // State 3 = M1
    timingNets_[4] = findNet("M22"); // State 4 = M2
    timingNets_[5] = findNet("X12"); // State 5 = X1
    timingNets_[6] = findNet("X22"); // State 6 = X2
    timingNets_[7] = findNet("X32"); // State 7 = X3
    syncNet_ = findNet("SYNC");
    execCtrlNet_ = findNet("~(X31&~CLK2)");
    execCtrl2Net_ = findNet("~(X21&~CLK2)");
    compNets_.inhX11X31Clk1 = findNet("(~INH)(X11+X31)CLK1");
    compNets_.pocClk2A32X12 = findNet("(~POC)&CLK2&SC(A32+X12)");
    compNets_.clk2A12M12 = findNet("CLK2&SC(A12+M12)");
    compNets_.m12m22Clk1 = findNet("M12+M22+CLK1~(M11+M12)");
    compNets_.scA22M22Clk2 = findNet("SC(A22+M22)CLK2");
    compNets_.scM22Clk2 = findNet("SC&M22&CLK2");
    compNets_.scM12Clk2 = findNet("SC&M12&CLK2");
    compNets_.scA12Clk2 = findNet("SC&A12&CLK2");
    compNets_.scA22 = findNet("SC&A22");
    compNets_.inhX32Clk2 = findNet("(~INH)&X32&CLK2");
    compNets_.pocClk2X12X32 = findNet("(~POC)CLK2(X12+X32)~INH");
    compNets_.jmsDcBbl = findNet("CLK2(JMS&DC&M22+BBL(M22+X12+X22))");
    compNets_.scJinFin = findNet("((~SC)(JIN+FIN))CLK1(M11+X21~INH)");

    // Index registers R0-R15 (4 bits each)
    for (std::size_t r = 0; r < 16; ++r) {
      const std::string PREFIX = "R" + std::to_string(r) + ".";
      for (std::size_t b = 0; b < 4; ++b) {
        regNets_[r][b] = findNet(PREFIX + std::to_string(b));
      }
    }

    // Program counter PC0.0-PC0.11
    for (std::size_t b = 0; b < 12; ++b) {
      pcNets_[b] = findNet("PC0." + std::to_string(b));
    }
  }

  /* ----------------------------- Transistor Resolution ----------------------------- */

  void resolveTransistors(const Intel4004Netlist& netlist) {
    transistors_.clear();
    transistors_.reserve(netlist.transistorCount());

    for (const auto& t : netlist.transistors) {
      ResolvedTransistor rt{};
      rt.drain = resolveNet(t.drain);
      rt.gate = resolveNet(t.gate);
      rt.source = resolveNet(t.source);
      // Pull-up transistors: any transistor with a terminal at VDD is in a
      // pull-up configuration. In PMOS enhancement-load logic, pull-ups are
      // intentionally weaker than pull-downs (smaller W/L ratio). This includes:
      // - Depletion-mode loads (gate=VDD, terminal=VDD): always-ON static loads
      // - Signal-controlled pull-ups (terminal=VDD): conditional pull-ups
      // Without this asymmetry, contested nodes sit at VDD/2 = 2.5V.
      rt.isLoad = (rt.drain == vdd_ || rt.source == vdd_);
      // Depletion-mode load: gate shorted to VDD with one terminal at VDD.
      // SPICE drain/source labels are arbitrary for symmetric MOSFETs, so
      // check both orderings.
      rt.isDiodeLoad = (rt.gate == vdd_ && (rt.drain == vdd_ || rt.source == vdd_));
      transistors_.push_back(rt);
    }

    identifyStaticVddNets();
  }

  /**
   * @brief Identify static VDD nets and upgrade cascaded depletion loads.
   *
   * In PMOS circuits, depletion-mode load transistors (gate=VDD, terminal=VDD)
   * are always ON. Some circuits cascade loads: a primary depletion load charges
   * an internal net to VDD, and that net gates a secondary load transistor. The
   * secondary should also be always-ON, but isDiodeLoad misses it because its
   * gate is a different sim::electronics::algorithms::mna::NetID from VDD.
   *
   * Algorithm:
   * 1. Collect output nets of isDiodeLoad transistors.
   * 2. Remove any that have non-load transistor connections (potential pull-down).
   * 3. Upgrade isLoad transistors gated by surviving "static VDD" nets.
   * 4. Repeat until stable (handles chains of depth > 2).
   *
   * Example: M1611 (VDD->S00716, depletion load) charges S00716 to VDD.
   * M1599 (VDD->OPA-IB, gate=S00716) should be always-ON but was missed.
   * S00716 has no pull-down -> static VDD -> M1599 upgraded to isDiodeLoad.
   */
  void identifyStaticVddNets() {
    bool changed = true;
    while (changed) {
      changed = false;

      // Collect output nets of depletion loads
      std::unordered_set<sim::electronics::algorithms::mna::NetID> candidates;
      for (const auto& t : transistors_) {
        if (!t.isDiodeLoad) {
          continue;
        }
        sim::electronics::algorithms::mna::NetID output = (t.drain == vdd_) ? t.source : t.drain;
        if (output != 0 && output != vdd_) {
          candidates.insert(output);
        }
      }

      // Remove candidates with non-load transistor connections
      for (const auto& t : transistors_) {
        if (t.isLoad) {
          continue;
        }
        candidates.erase(t.drain);
        candidates.erase(t.source);
      }

      // Upgrade load transistors gated by static VDD nets
      for (auto& t : transistors_) {
        if (t.isLoad && !t.isDiodeLoad && candidates.count(t.gate)) {
          t.isDiodeLoad = true;
          changed = true;
        }
      }
    }
  }

  sim::electronics::algorithms::mna::NetID resolveNet(const std::string& name) const {
    auto it = netMap_.find(name);
    return (it != netMap_.end()) ? it->second : 0;
  }

  /* ----------------------------- Stamp Registration ----------------------------- */

  void registerStamps(Circuit& circuit) {
    circuit.addStamp(
        [this](sim::electronics::algorithms::mna::MnaSystem& mna, double, const std::vector<double>&) {
          mna.addVoltageSource(vdd_, Circuit::ground(), VDD_VOLTAGE);
        });

    circuit.addStamp([this](sim::electronics::algorithms::mna::MnaSystem& mna, double /*time*/,
                            const std::vector<double>& prevV) {
      stampTransistors(mna, prevV);
      stampParasiticCaps(mna, prevV);
    });

    circuit.addStamp([this](sim::electronics::algorithms::mna::MnaSystem& mna, double,
                            const std::vector<double>&) { stampExternalIO(mna); });
  }

  /* ----------------------------- Stamping ----------------------------- */

  /**
   * @brief Stamp all PMOS transistors as voltage-controlled conductances.
   * @note RT-safe.
   */
  template <typename MnaSystemT>
  void stampTransistors(MnaSystemT& mna, const std::vector<double>& prevV) const {
    for (std::size_t idx = 0; idx < transistors_.size(); ++idx) {
      const auto& t = transistors_[idx];

      // Binary switch: three-region conductance model.
      // Uses bsParams_ for MC-tunable parameters (defaults match static constexpr).
      const auto& bp = bsParams_;
      double vs = std::max(prevV[t.drain], prevV[t.source]);
      double vgs = prevV[t.gate] - vs;

      double gds;
      if (t.isDiodeLoad) {
        gds = bp.gLoad;
      } else if (vgs < -bp.vth - bp.subthMargin) {
        gds = t.isLoad ? bp.gLoad : bp.gOn;
      } else if (vgs > -bp.vth + bp.subthMargin) {
        gds = bp.gOff;
      } else {
        gds = bp.gSubth;
      }

      mna.addConductance(t.drain, t.source, gds);
    }
  }

  /**
   * @brief Stamp external IO: CLK1, CLK2, and data bus (during M1/M2).
   * @note RT-safe.
   */
  template <typename MnaSystemT> void stampExternalIO(MnaSystemT& mna) const {
    driveNet(mna, clk1Net_, clk1High_);
    driveNet(mna, clk2Net_, clk2High_);
    if (dataBusDriving_) {
      driveBus(mna, dataBusNets_.data(), 4, dataBusDrive_);
    }
    // Behavioral timing injection: drive machine state signals directly.
    // Bypasses the timing generator ring counter (which can't bootstrap).
    if (behavioralTiming_) {
      // Each state has TWO timing signals:
      //   *1 = CLK1-phase (active during CLK1 sub-phase of that state)
      //   *2 = CLK2-phase (active during CLK2 sub-phase of that state)
      //
      // timingNets_[i] = CLK2-phase signal (A12, A22, ..., X32)
      // timingNets1_[i] = CLK1-phase signal (computed from compound nets)
      //
      // Active = LOW in PMOS convention.
      bool clk1Active = !clk1High_;
      bool clk2Active = !clk2High_;
      bool sync = (machineState_ <= 2);
      std::uint8_t ms = machineState_;

      for (std::size_t i = 0; i < 8; ++i) {
        if (timingNets_[i] > 0) {
          // Active (LOW) for the full machine state.
          bool active = (machineState_ == i);
          driveNet(mna, timingNets_[i], !active);
        }
      }
      // SYNC: HIGH during address phases (A1-A3), LOW during M1-X3
      if (syncNet_ > 0) {
        bool inAddressPhase = (machineState_ <= 2);
        driveNet(mna, syncNet_, inAddressPhase);
      }
      // Direct drive of key latch and decoder control signals.

      // OPA-IB: controls data bus -> OPA pass gates.
      // Active (LOW) during M2 to latch low nibble (OPA/operand).
      auto opaIbNet = findNet("OPA-IB");
      if (opaIbNet > 0) {
        driveNet(mna, opaIbNet, !(ms == 4)); // LOW during M2
      }

      // SC&M12&CLK2: controls OPR latch pass gates.
      // Active (LOW) during M1 state CLK2 phase to latch high nibble from bus.
      // SC is a dynamic node at marginal voltage in Level 1 (Entry 24: 1.59V).
      // Drive the compound signal directly during M1 only.
      auto scM12Clk2 = findNet("SC&M12&CLK2");
      if (scM12Clk2 > 0) {
        // LOW during M1 (state 3), HIGH otherwise
        bool m1Active = (ms == 3) && !clk2High_; // M1 + CLK2 active
        driveNet(mna, scM12Clk2, !m1Active);
      }

      // Note: execute control (~(X31&~CLK2)) is left to the transistors.
      // Driving it behaviorally breaks single-instruction execution.
      // SC&M22&CLK2 also left to transistors for now (behavioral latch
      // handles OPA storage).
    }
  }

  /**
   * @brief Stamp parasitic node capacitance for dynamic logic support.
   *
   * The 4004 uses dynamic PMOS logic: charge stored on gate capacitances
   * holds state between clock phases. Without parasitic capacitance, the
   * switch-level model cannot represent charge storage.
   *
   * Backward Euler companion: Geq = C/stepDt_ conductance + Ieq = Geq*Vprev
   * current source at every internal node. Skipped during DC (stepDt_ == 0).
   *
   * Uses stepDt_ member (not the stamp callback's time parameter) because
   * the solver passes simulation time, not the timestep, to stamp callbacks.
   *
   * @note RT-safe.
   */
  template <typename MnaSystemT>
  void stampParasiticCaps(MnaSystemT& mna, const std::vector<double>& prevV) const {
    if (stepDt_ <= 0.0) {
      return;
    }
    const double GEQ = bsParams_.cpara / stepDt_;
    const std::size_t N = prevV.size();
    for (sim::electronics::algorithms::mna::NetID n = 1; n < N; ++n) {
      if (n == vdd_) {
        continue;
      }
      mna.addConductance(n, Circuit::ground(), GEQ);
      mna.addCurrent(n, Circuit::ground(), GEQ * prevV[n]);
    }
  }

  /* ----------------------------- Simulation Helpers ----------------------------- */

  void runSubSteps(Circuit& circuit, sim::electronics::algorithms::transient::TransientState& state,
                   std::size_t steps, double dt, bool useConvergence, double threshold,
                   std::size_t netCount, std::vector<double>& prevSubV) {
    stepDt_ = dt;
    for (std::size_t s = 0; s < steps; ++s) {
      if (useConvergence && s > 0) {
        std::memcpy(prevSubV.data(), state.nodeVoltages.data(), netCount * sizeof(double));
      }
      // Fresh factorize each sub-step: with 1080 nets and parasitic caps,
      // transistors near threshold can flip between steps, invalidating the
      // cached LU. Correctness over caching for now.
      circuit.solver().invalidateCache();
      circuit.step(dt, state);
      if (useConvergence && s >= 1) {
        double maxDelta = 0.0;
        for (std::size_t i = 1; i < netCount; ++i) {
          double d = state.nodeVoltages[i] - prevSubV[i];
          if (d < 0.0)
            d = -d;
          if (d > maxDelta)
            maxDelta = d;
        }
        if (maxDelta < threshold)
          break;
      }
    }
  }

  /**
   * @brief Execute the byte-fetch simulation loop on an existing state.
   *
   * Drives CLK1/CLK2 through 8 machine states per byte fetch, feeding ROM
   * data on D0-D3 during M1/M2 and capturing addresses during A1-A3.
   * Extracted from simulate() to support two-phase simulation (e.g., binary
   * switch warm-up followed by Level 1 execution).
   *
   * @param circuit Circuit with stamp callbacks already configured.
   * @param state Transient state (modified in-place).
   * @param rom ROM byte array.
   * @param romSize Size of ROM array.
   * @param startByte Index of first byte to fetch from ROM.
   * @param numBytes Number of bytes to fetch.
   * @param subStep Timestep per sub-step (clockPeriod / 4 / stepsPerPhase).
   * @param stepsPerPhase Sub-steps per clock phase.
   * @param useConvergence Enable early termination per sub-phase.
   * @param convergenceThreshold Max voltage delta for early termination.
   * @param prevSubV Scratch buffer for convergence detection (pre-allocated).
   *
   * @note NOT RT-safe: modifies simulation state members.
   */
  void runByteLoop(Circuit& circuit, sim::electronics::algorithms::transient::TransientState& state,
                   const std::uint8_t* rom, std::size_t romSize, std::size_t startByte,
                   std::size_t numBytes, double subStep, std::size_t stepsPerPhase,
                   bool useConvergence, double convergenceThreshold,
                   std::vector<double>& prevSubV) {
    const std::size_t NET_COUNT = state.nodeVoltages.size();

    for (std::size_t byteIdx = startByte; byteIdx < startByte + numBytes; ++byteIdx) {
      std::uint8_t romByte = (byteIdx < romSize) ? rom[byteIdx] : 0x00;

      // L1+ pre-execution refresh: restore ACC/CY to clean rail voltages
      // BEFORE executing this byte. This prevents charge leakage during the
      // previous byte's execution from corrupting the stored values.
      if (refreshStorage_ && bytesFetched_ > 0) {
        std::uint8_t acc = readAccumulator(state.nodeVoltages);
        for (int bit = 0; bit < 4; ++bit) {
          if (accNets_[bit] > 0 && accNets_[bit] < state.nodeVoltages.size()) {
            state.nodeVoltages[accNets_[bit]] = (acc & (1 << bit)) ? 0.0 : VDD_VOLTAGE;
          }
        }
        if (cyNet_ > 0 && cyNet_ < state.nodeVoltages.size()) {
          bool cy = readCarry(state.nodeVoltages);
          state.nodeVoltages[cyNet_] = cy ? 0.0 : VDD_VOLTAGE;
        }
      }

      for (std::uint8_t ms = 0; ms < 8; ++ms) {
        machineState_ = ms;

        if (ms == 3) {
          dataBusDriving_ = true;
          dataBusDrive_ = (romByte >> 4) & 0x0F;
        } else if (ms == 4) {
          dataBusDriving_ = true;
          dataBusDrive_ = romByte & 0x0F;
        } else {
          dataBusDriving_ = false;
        }

        // Sub-phase 1: CLK1 active (LOW), CLK2 inactive (HIGH)
        clk1High_ = false;
        clk2High_ = true;
        circuit.solver().invalidateCache();
        runSubSteps(circuit, state, stepsPerPhase, subStep, useConvergence, convergenceThreshold,
                    NET_COUNT, prevSubV);

        // Sub-phase 2: Dead zone (both HIGH)
        clk1High_ = true;
        clk2High_ = true;
        circuit.solver().invalidateCache();
        runSubSteps(circuit, state, stepsPerPhase, subStep, useConvergence, convergenceThreshold,
                    NET_COUNT, prevSubV);

        // Sub-phase 3: CLK2 active (LOW), CLK1 inactive (HIGH)
        clk1High_ = true;
        clk2High_ = false;
        circuit.solver().invalidateCache();
        runSubSteps(circuit, state, stepsPerPhase, subStep, useConvergence, convergenceThreshold,
                    NET_COUNT, prevSubV);

        // Capture address during A1-A3
        if (ms <= 2) {
          std::uint8_t nibble = readDataBus(state.nodeVoltages);
          if (ms == 0) {
            lastCapturedAddr_ = nibble;
          } else if (ms == 1) {
            lastCapturedAddr_ |= (static_cast<std::uint16_t>(nibble) << 4);
          } else {
            lastCapturedAddr_ |= (static_cast<std::uint16_t>(nibble) << 8);
          }
        }

        // Sub-phase 4: Dead zone (both HIGH)
        clk1High_ = true;
        clk2High_ = true;
        dataBusDriving_ = false;
        circuit.solver().invalidateCache();
        runSubSteps(circuit, state, stepsPerPhase, subStep, useConvergence, convergenceThreshold,
                    NET_COUNT, prevSubV);
      }

      ++bytesFetched_;

      // L1+ instruction-boundary refresh: force ACC and CY nodes to clean
      // rail voltages (0V or 5V) based on current readback. This prevents
      // charge leakage between instruction cycles. The binary switch model
      // can't retain charge on dynamic storage nodes (confirmed by MC sweep,
      // entry #16). This refresh mimics the real chip's precharge/evaluate
      // cycle that restores logic levels each clock cycle.
      if (refreshStorage_) {
        std::uint8_t acc = readAccumulator(state.nodeVoltages);
        for (int bit = 0; bit < 4; ++bit) {
          if (accNets_[bit] > 0 && accNets_[bit] < state.nodeVoltages.size()) {
            // PMOS: bit=1 -> LOW voltage, bit=0 -> HIGH voltage
            state.nodeVoltages[accNets_[bit]] = (acc & (1 << bit)) ? 0.0 : VDD_VOLTAGE;
          }
        }
        if (cyNet_ > 0 && cyNet_ < state.nodeVoltages.size()) {
          bool cy = readCarry(state.nodeVoltages);
          state.nodeVoltages[cyNet_] = cy ? 0.0 : VDD_VOLTAGE;
        }
      }
    }
  }

public:
  bool refreshStorage_ = false; ///< L1+ mode: refresh ACC/CY at instruction boundaries.
};

} // namespace sim::electronics::chips::intel4004

#endif // APEX_INTEL4004GRID_HPP
