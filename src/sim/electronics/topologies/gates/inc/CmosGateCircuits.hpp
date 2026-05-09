#ifndef APEX_CMOSGATECIRCUITS_HPP
#define APEX_CMOSGATECIRCUITS_HPP
/**
 * @file CmosGateCircuits.hpp
 * @brief Circuit-level CMOS gate models using the Circuit API and MNA solver.
 *
 * All 7 standard CMOS gate types solved through the MNA infrastructure with
 * MosfetLevel1 (Shichman-Hodges) physics. Each gate builds a Circuit with
 * transistor-level stamp callbacks that linearize around the previous
 * iteration's node voltages, converging via Newton-Raphson to the DC
 * operating point.
 *
 * Gate types and transistor counts:
 *   NOT  (CmosInverterCircuit)  --  2 MOSFETs (1 PMOS + 1 NMOS)
 *   NAND (CmosNandCircuit)      --  4 MOSFETs (2 PMOS + 2 NMOS)
 *   NOR  (CmosNorCircuit)       --  4 MOSFETs (2 PMOS + 2 NMOS)
 *   AND  (CmosAndCircuit)       --  6 MOSFETs (NAND + NOT buffer)
 *   OR   (CmosOrCircuit)        --  6 MOSFETs (NOR + NOT buffer)
 *   XOR  (CmosXorCircuit)       -- 16 MOSFETs (4 NAND gates)
 *   XNOR (CmosXnorCircuit)     -- 18 MOSFETs (XOR + NOT buffer)
 *
 * @note NOT RT-safe (iterative solver allocates during computation).
 */

#include "src/sim/electronics/circuit/inc/Circuit.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1.hpp"

#include <vector>

namespace sim::electronics::topologies::gates {

using circuit::Circuit;
using circuit::CircuitNet;
using devices::nonlinear::MosfetLevel1;
using devices::nonlinear::MosfetLevel1Params;
using algorithms::mna::MnaSystem;
using algorithms::mna::NetID;
using algorithms::transient::TransientState;
using algorithms::transient::TransientStatus;

/* ----------------------------- Constants ----------------------------- */

/// Maximum Newton-Raphson iterations for DC convergence.
static constexpr int NR_MAX_ITER = 200;

/// Voltage convergence tolerance for NR iteration (volts).
static constexpr double NR_TOL = 1e-6;

/// Minimum conductance (GMIN) shunt to ground for NR convergence.
/// Standard SPICE technique: prevents singular Jacobian when all transistors
/// are in cutoff during early NR iterations by providing a leakage path.
static constexpr double GMIN = 1e-9;

/* ----------------------------- File Helpers ----------------------------- */

/**
 * @brief Stamp a PMOS transistor using MosfetLevel1 base module.
 *
 * Delegates to MosfetLevel1::stampPmos with the gate-circuit GMIN constant.
 */
static inline void stampPmos(MnaSystem& mna, NetID sourceNet, NetID gateNet, NetID drainNet,
                             const std::vector<double>& prevV, const MosfetLevel1Params& params) {
  MosfetLevel1::stampPmos(mna, sourceNet, gateNet, drainNet, prevV, params, GMIN);
}

/**
 * @brief Stamp an NMOS transistor using MosfetLevel1 base module.
 *
 * Delegates to MosfetLevel1::stampNmos with the gate-circuit GMIN constant.
 */
static inline void stampNmos(MnaSystem& mna, NetID drainNet, NetID gateNet, NetID sourceNet,
                             const std::vector<double>& prevV, const MosfetLevel1Params& params) {
  MosfetLevel1::stampNmos(mna, drainNet, gateNet, sourceNet, prevV, params, GMIN);
}

/**
 * @brief Run NR iteration loop with explicit convergence tracking.
 *
 * Resets the solver to start from zero prevVoltages, then calls
 * circuit.computeDC() repeatedly. Each call stamps the MNA system
 * using prevVoltages from the solver (updated after each solve), so
 * successive calls linearize around better operating points until
 * convergence.
 *
 * @param circuit Circuit instance (build() called automatically if needed).
 * @param state TransientState for solution storage.
 * @param netCount Number of nets to check for convergence.
 * @return Final status after NR convergence.
 */
static inline TransientStatus solveNRConverge(Circuit& circuit, TransientState& state,
                                              std::size_t /*netCount*/) {
  circuit.resetSolver();
  circuit.build();

  std::vector<double> prevSolution(circuit.netCount(), 0.0);
  TransientStatus status = TransientStatus::ERROR_DC_FAILED;

  for (int iter = 0; iter < NR_MAX_ITER; ++iter) {
    status = circuit.computeDC(state);
    if (status != TransientStatus::SUCCESS) {
      break;
    }

    double maxDelta = 0.0;
    for (std::size_t i = 0; i < circuit.netCount() && i < state.nodeVoltages.size(); ++i) {
      double DELTA = state.nodeVoltages[i] - prevSolution[i];
      maxDelta = std::max(maxDelta, std::abs(DELTA));
    }

    auto& solverPrevV = circuit.solver().prevVoltages();
    for (std::size_t i = 0; i < circuit.netCount() && i < solverPrevV.size(); ++i) {
      solverPrevV[i] = state.nodeVoltages[i];
    }

    if (maxDelta < NR_TOL) {
      break;
    }
    prevSolution = state.nodeVoltages;
  }
  return status;
}

/* ----------------------------- CmosInverterCircuit ----------------------------- */

/**
 * @brief Circuit-level CMOS inverter (NOT gate) -- 2 MOSFETs.
 *
 * Topology:
 *
 *     VDD
 *      |
 *     (PMOS)  source=VDD, gate=IN, drain=OUT
 *      |
 *      +--- OUT
 *      |
 *     (NMOS)  drain=OUT, gate=IN, source=GND
 *      |
 *     GND
 *
 * Usage:
 * @code
 * MosfetLevel1Params nmos{.Kp = 120e-6, .Vth = 0.7};
 * MosfetLevel1Params pmos{.Kp = 60e-6, .Vth = 0.7};
 * CmosInverterCircuit inv(5.0, 10e-6, 1e-6, nmos, pmos);
 * inv.build();
 * inv.setInput(0.0);
 * double vout = inv.computeDC();  // Near 5.0V
 * @endcode
 *
 * @note NOT RT-safe.
 */
struct CmosInverterCircuit {

  /* ----------------------------- Construction ----------------------------- */

  /**
   * @brief Construct a circuit-level CMOS inverter.
   * @param vdd Supply voltage in volts.
   * @param W Channel width for both transistors (meters).
   * @param L Channel length for both transistors (meters).
   * @param nmosParams NMOS transistor parameters.
   * @param pmosParams PMOS transistor parameters (Vth positive).
   */
  CmosInverterCircuit(double vdd, double /*W*/, double /*L*/, const MosfetLevel1Params& nmosParams,
                      const MosfetLevel1Params& pmosParams) noexcept
      : vdd_(vdd), nmosParams_(nmosParams), pmosParams_(pmosParams) {
    vddNet_ = circuit_.addNet("VDD").id;
    inNet_ = circuit_.addNet("IN").id;
    outNet_ = circuit_.addNet("OUT").id;

    circuit_.addStamp([this](MnaSystem& mna, double, const std::vector<double>& prevV) {
      mna.addVoltageSource(vddNet_, Circuit::ground(), vdd_);
      mna.addVoltageSource(inNet_, Circuit::ground(), inputV_);
      stampPmos(mna, vddNet_, inNet_, outNet_, prevV, pmosParams_);
      stampNmos(mna, outNet_, inNet_, Circuit::ground(), prevV, nmosParams_);
      // GMIN shunt on output node for NR convergence
      mna.addConductance(outNet_, Circuit::ground(), GMIN);
    });
  }

  /* ----------------------------- API ----------------------------- */

  /// Build the circuit solver.
  void build() { circuit_.build(); }

  /// Set the input voltage.
  void setInput(double vin) noexcept { inputV_ = vin; }

  /**
   * @brief Compute DC output voltage via MNA solver with NR iteration.
   * @return Output voltage in volts.
   */
  [[nodiscard]] double computeDC() {
    TransientState state;
    solveNRConverge(circuit_, state, circuit_.netCount());
    return (outNet_ < state.nodeVoltages.size()) ? state.nodeVoltages[outNet_] : 0.0;
  }

  /// Get the output net ID for probing.
  [[nodiscard]] NetID outputNet() const noexcept { return outNet_; }

  /// Get the VDD supply voltage.
  [[nodiscard]] double vdd() const noexcept { return vdd_; }

private:
  double vdd_;
  MosfetLevel1Params nmosParams_;
  MosfetLevel1Params pmosParams_;
  double inputV_ = 0.0;
  Circuit circuit_;
  NetID vddNet_ = 0;
  NetID inNet_ = 0;
  NetID outNet_ = 0;
};

/* ----------------------------- CmosNandCircuit ----------------------------- */

/**
 * @brief Circuit-level CMOS NAND gate -- 4 MOSFETs.
 *
 * Topology:
 *
 *       VDD
 *        |
 *    +---+---+
 *    |       |
 *   (P1)    (P2)   PMOS pull-ups (parallel, source=VDD, drain=OUT)
 *    |       |
 *    +---+---+
 *        |
 *       OUT
 *        |
 *       (N1)       NMOS (drain=OUT, gate=A, source=MID)
 *        |
 *       MID
 *        |
 *       (N2)       NMOS (drain=MID, gate=B, source=GND)
 *        |
 *       GND
 *
 * @note NOT RT-safe.
 */
struct CmosNandCircuit {

  /* ----------------------------- Construction ----------------------------- */

  CmosNandCircuit(double vdd, double /*W*/, double /*L*/, const MosfetLevel1Params& nmosParams,
                  const MosfetLevel1Params& pmosParams) noexcept
      : vdd_(vdd), nmosParams_(nmosParams), pmosParams_(pmosParams) {
    vddNet_ = circuit_.addNet("VDD").id;
    inANet_ = circuit_.addNet("INA").id;
    inBNet_ = circuit_.addNet("INB").id;
    outNet_ = circuit_.addNet("OUT").id;
    midNet_ = circuit_.addNet("MID").id;

    circuit_.addStamp([this](MnaSystem& mna, double, const std::vector<double>& prevV) {
      mna.addVoltageSource(vddNet_, Circuit::ground(), vdd_);
      mna.addVoltageSource(inANet_, Circuit::ground(), inputA_);
      mna.addVoltageSource(inBNet_, Circuit::ground(), inputB_);
      // P1: source=VDD, gate=A, drain=OUT (parallel)
      stampPmos(mna, vddNet_, inANet_, outNet_, prevV, pmosParams_);
      // P2: source=VDD, gate=B, drain=OUT (parallel)
      stampPmos(mna, vddNet_, inBNet_, outNet_, prevV, pmosParams_);
      // N1: drain=OUT, gate=A, source=MID (series)
      stampNmos(mna, outNet_, inANet_, midNet_, prevV, nmosParams_);
      // N2: drain=MID, gate=B, source=GND (series)
      stampNmos(mna, midNet_, inBNet_, Circuit::ground(), prevV, nmosParams_);
      // GMIN shunts on floating nodes
      mna.addConductance(outNet_, Circuit::ground(), GMIN);
      mna.addConductance(midNet_, Circuit::ground(), GMIN);
    });
  }

  /* ----------------------------- API ----------------------------- */

  void build() { circuit_.build(); }

  void setInputs(double vinA, double vinB) noexcept {
    inputA_ = vinA;
    inputB_ = vinB;
  }

  [[nodiscard]] double computeDC() {
    TransientState state;
    solveNRConverge(circuit_, state, circuit_.netCount());
    return (outNet_ < state.nodeVoltages.size()) ? state.nodeVoltages[outNet_] : 0.0;
  }

  [[nodiscard]] NetID outputNet() const noexcept { return outNet_; }
  [[nodiscard]] double vdd() const noexcept { return vdd_; }

private:
  double vdd_;
  MosfetLevel1Params nmosParams_;
  MosfetLevel1Params pmosParams_;
  double inputA_ = 0.0;
  double inputB_ = 0.0;
  Circuit circuit_;
  NetID vddNet_ = 0;
  NetID inANet_ = 0;
  NetID inBNet_ = 0;
  NetID outNet_ = 0;
  NetID midNet_ = 0;
};

/* ----------------------------- CmosNorCircuit ----------------------------- */

/**
 * @brief Circuit-level CMOS NOR gate -- 4 MOSFETs.
 *
 * Topology:
 *
 *       VDD
 *        |
 *       (P1)       PMOS (source=VDD, gate=A, drain=MID)
 *        |
 *       MID
 *        |
 *       (P2)       PMOS (source=MID, gate=B, drain=OUT)
 *        |
 *       OUT
 *        |
 *    +---+---+
 *    |       |
 *   (N1)    (N2)   NMOS pull-downs (parallel, drain=OUT, source=GND)
 *    |       |
 *    +---+---+
 *        |
 *       GND
 *
 * @note NOT RT-safe.
 */
struct CmosNorCircuit {

  /* ----------------------------- Construction ----------------------------- */

  CmosNorCircuit(double vdd, double /*W*/, double /*L*/, const MosfetLevel1Params& nmosParams,
                 const MosfetLevel1Params& pmosParams) noexcept
      : vdd_(vdd), nmosParams_(nmosParams), pmosParams_(pmosParams) {
    vddNet_ = circuit_.addNet("VDD").id;
    inANet_ = circuit_.addNet("INA").id;
    inBNet_ = circuit_.addNet("INB").id;
    outNet_ = circuit_.addNet("OUT").id;
    midNet_ = circuit_.addNet("MID").id;

    circuit_.addStamp([this](MnaSystem& mna, double, const std::vector<double>& prevV) {
      mna.addVoltageSource(vddNet_, Circuit::ground(), vdd_);
      mna.addVoltageSource(inANet_, Circuit::ground(), inputA_);
      mna.addVoltageSource(inBNet_, Circuit::ground(), inputB_);
      // P1: source=VDD, gate=A, drain=MID (series)
      stampPmos(mna, vddNet_, inANet_, midNet_, prevV, pmosParams_);
      // P2: source=MID, gate=B, drain=OUT (series)
      stampPmos(mna, midNet_, inBNet_, outNet_, prevV, pmosParams_);
      // N1: drain=OUT, gate=A, source=GND (parallel)
      stampNmos(mna, outNet_, inANet_, Circuit::ground(), prevV, nmosParams_);
      // N2: drain=OUT, gate=B, source=GND (parallel)
      stampNmos(mna, outNet_, inBNet_, Circuit::ground(), prevV, nmosParams_);
      // GMIN shunts on floating nodes
      mna.addConductance(outNet_, Circuit::ground(), GMIN);
      mna.addConductance(midNet_, Circuit::ground(), GMIN);
    });
  }

  /* ----------------------------- API ----------------------------- */

  void build() { circuit_.build(); }

  void setInputs(double vinA, double vinB) noexcept {
    inputA_ = vinA;
    inputB_ = vinB;
  }

  [[nodiscard]] double computeDC() {
    TransientState state;
    solveNRConverge(circuit_, state, circuit_.netCount());
    return (outNet_ < state.nodeVoltages.size()) ? state.nodeVoltages[outNet_] : 0.0;
  }

  [[nodiscard]] NetID outputNet() const noexcept { return outNet_; }
  [[nodiscard]] double vdd() const noexcept { return vdd_; }

private:
  double vdd_;
  MosfetLevel1Params nmosParams_;
  MosfetLevel1Params pmosParams_;
  double inputA_ = 0.0;
  double inputB_ = 0.0;
  Circuit circuit_;
  NetID vddNet_ = 0;
  NetID inANet_ = 0;
  NetID inBNet_ = 0;
  NetID outNet_ = 0;
  NetID midNet_ = 0;
};

/* ----------------------------- CmosAndCircuit ----------------------------- */

/**
 * @brief Circuit-level CMOS AND gate -- 6 MOSFETs (NAND + NOT buffer).
 *
 * Topology: NAND(A,B) feeding an inverter to produce AND(A,B).
 *
 *   A,B --> [NAND] --nandOut--> [NOT] --> OUT
 *
 * @note NOT RT-safe.
 */
struct CmosAndCircuit {

  /* ----------------------------- Construction ----------------------------- */

  CmosAndCircuit(double vdd, double /*W*/, double /*L*/, const MosfetLevel1Params& nmosParams,
                 const MosfetLevel1Params& pmosParams) noexcept
      : vdd_(vdd), nmosParams_(nmosParams), pmosParams_(pmosParams) {
    vddNet_ = circuit_.addNet("VDD").id;
    inANet_ = circuit_.addNet("INA").id;
    inBNet_ = circuit_.addNet("INB").id;
    nandOutNet_ = circuit_.addNet("NAND_OUT").id;
    nandMidNet_ = circuit_.addNet("NAND_MID").id;
    outNet_ = circuit_.addNet("OUT").id;

    circuit_.addStamp([this](MnaSystem& mna, double, const std::vector<double>& prevV) {
      mna.addVoltageSource(vddNet_, Circuit::ground(), vdd_);
      mna.addVoltageSource(inANet_, Circuit::ground(), inputA_);
      mna.addVoltageSource(inBNet_, Circuit::ground(), inputB_);

      // NAND stage: P1, P2 parallel pull-up; N1, N2 series pull-down
      stampPmos(mna, vddNet_, inANet_, nandOutNet_, prevV, pmosParams_);
      stampPmos(mna, vddNet_, inBNet_, nandOutNet_, prevV, pmosParams_);
      stampNmos(mna, nandOutNet_, inANet_, nandMidNet_, prevV, nmosParams_);
      stampNmos(mna, nandMidNet_, inBNet_, Circuit::ground(), prevV, nmosParams_);

      // NOT buffer: PMOS pull-up, NMOS pull-down
      stampPmos(mna, vddNet_, nandOutNet_, outNet_, prevV, pmosParams_);
      stampNmos(mna, outNet_, nandOutNet_, Circuit::ground(), prevV, nmosParams_);

      // GMIN shunts on floating nodes
      mna.addConductance(nandOutNet_, Circuit::ground(), GMIN);
      mna.addConductance(nandMidNet_, Circuit::ground(), GMIN);
      mna.addConductance(outNet_, Circuit::ground(), GMIN);
    });
  }

  /* ----------------------------- API ----------------------------- */

  void build() { circuit_.build(); }

  void setInputs(double vinA, double vinB) noexcept {
    inputA_ = vinA;
    inputB_ = vinB;
  }

  [[nodiscard]] double computeDC() {
    TransientState state;
    solveNRConverge(circuit_, state, circuit_.netCount());
    return (outNet_ < state.nodeVoltages.size()) ? state.nodeVoltages[outNet_] : 0.0;
  }

  [[nodiscard]] NetID outputNet() const noexcept { return outNet_; }
  [[nodiscard]] double vdd() const noexcept { return vdd_; }

private:
  double vdd_;
  MosfetLevel1Params nmosParams_;
  MosfetLevel1Params pmosParams_;
  double inputA_ = 0.0;
  double inputB_ = 0.0;
  Circuit circuit_;
  NetID vddNet_ = 0;
  NetID inANet_ = 0;
  NetID inBNet_ = 0;
  NetID nandOutNet_ = 0;
  NetID nandMidNet_ = 0;
  NetID outNet_ = 0;
};

/* ----------------------------- CmosOrCircuit ----------------------------- */

/**
 * @brief Circuit-level CMOS OR gate -- 6 MOSFETs (NOR + NOT buffer).
 *
 * Topology: NOR(A,B) feeding an inverter to produce OR(A,B).
 *
 *   A,B --> [NOR] --norOut--> [NOT] --> OUT
 *
 * @note NOT RT-safe.
 */
struct CmosOrCircuit {

  /* ----------------------------- Construction ----------------------------- */

  CmosOrCircuit(double vdd, double /*W*/, double /*L*/, const MosfetLevel1Params& nmosParams,
                const MosfetLevel1Params& pmosParams) noexcept
      : vdd_(vdd), nmosParams_(nmosParams), pmosParams_(pmosParams) {
    vddNet_ = circuit_.addNet("VDD").id;
    inANet_ = circuit_.addNet("INA").id;
    inBNet_ = circuit_.addNet("INB").id;
    norOutNet_ = circuit_.addNet("NOR_OUT").id;
    norMidNet_ = circuit_.addNet("NOR_MID").id;
    outNet_ = circuit_.addNet("OUT").id;

    circuit_.addStamp([this](MnaSystem& mna, double, const std::vector<double>& prevV) {
      mna.addVoltageSource(vddNet_, Circuit::ground(), vdd_);
      mna.addVoltageSource(inANet_, Circuit::ground(), inputA_);
      mna.addVoltageSource(inBNet_, Circuit::ground(), inputB_);

      // NOR stage: P1, P2 series pull-up; N1, N2 parallel pull-down
      stampPmos(mna, vddNet_, inANet_, norMidNet_, prevV, pmosParams_);
      stampPmos(mna, norMidNet_, inBNet_, norOutNet_, prevV, pmosParams_);
      stampNmos(mna, norOutNet_, inANet_, Circuit::ground(), prevV, nmosParams_);
      stampNmos(mna, norOutNet_, inBNet_, Circuit::ground(), prevV, nmosParams_);

      // NOT buffer: PMOS pull-up, NMOS pull-down
      stampPmos(mna, vddNet_, norOutNet_, outNet_, prevV, pmosParams_);
      stampNmos(mna, outNet_, norOutNet_, Circuit::ground(), prevV, nmosParams_);

      // GMIN shunts on floating nodes
      mna.addConductance(norOutNet_, Circuit::ground(), GMIN);
      mna.addConductance(norMidNet_, Circuit::ground(), GMIN);
      mna.addConductance(outNet_, Circuit::ground(), GMIN);
    });
  }

  /* ----------------------------- API ----------------------------- */

  void build() { circuit_.build(); }

  void setInputs(double vinA, double vinB) noexcept {
    inputA_ = vinA;
    inputB_ = vinB;
  }

  [[nodiscard]] double computeDC() {
    TransientState state;
    solveNRConverge(circuit_, state, circuit_.netCount());
    return (outNet_ < state.nodeVoltages.size()) ? state.nodeVoltages[outNet_] : 0.0;
  }

  [[nodiscard]] NetID outputNet() const noexcept { return outNet_; }
  [[nodiscard]] double vdd() const noexcept { return vdd_; }

private:
  double vdd_;
  MosfetLevel1Params nmosParams_;
  MosfetLevel1Params pmosParams_;
  double inputA_ = 0.0;
  double inputB_ = 0.0;
  Circuit circuit_;
  NetID vddNet_ = 0;
  NetID inANet_ = 0;
  NetID inBNet_ = 0;
  NetID norOutNet_ = 0;
  NetID norMidNet_ = 0;
  NetID outNet_ = 0;
};

/* ----------------------------- CmosXorCircuit ----------------------------- */

/**
 * @brief Circuit-level CMOS XOR gate -- 16 MOSFETs (4 NAND gates).
 *
 * XOR(A,B) = NAND(NAND(A, NAND(A,B)), NAND(B, NAND(A,B)))
 *
 * Uses 4 NAND gates:
 *   NAND1: NAND(A, B) -> N1
 *   NAND2: NAND(A, N1) -> N2
 *   NAND3: NAND(B, N1) -> N3
 *   NAND4: NAND(N2, N3) -> OUT
 *
 * Each NAND is 4 MOSFETs (2 PMOS parallel + 2 NMOS series).
 *
 * @note NOT RT-safe.
 */
struct CmosXorCircuit {

  /* ----------------------------- Construction ----------------------------- */

  CmosXorCircuit(double vdd, double /*W*/, double /*L*/, const MosfetLevel1Params& nmosParams,
                 const MosfetLevel1Params& pmosParams) noexcept
      : vdd_(vdd), nmosParams_(nmosParams), pmosParams_(pmosParams) {
    vddNet_ = circuit_.addNet("VDD").id;
    inANet_ = circuit_.addNet("INA").id;
    inBNet_ = circuit_.addNet("INB").id;
    // NAND1 internals
    n1Out_ = circuit_.addNet("N1_OUT").id;
    n1Mid_ = circuit_.addNet("N1_MID").id;
    // NAND2 internals
    n2Out_ = circuit_.addNet("N2_OUT").id;
    n2Mid_ = circuit_.addNet("N2_MID").id;
    // NAND3 internals
    n3Out_ = circuit_.addNet("N3_OUT").id;
    n3Mid_ = circuit_.addNet("N3_MID").id;
    // NAND4 internals
    outNet_ = circuit_.addNet("OUT").id;
    n4Mid_ = circuit_.addNet("N4_MID").id;

    circuit_.addStamp([this](MnaSystem& mna, double, const std::vector<double>& prevV) {
      mna.addVoltageSource(vddNet_, Circuit::ground(), vdd_);
      mna.addVoltageSource(inANet_, Circuit::ground(), inputA_);
      mna.addVoltageSource(inBNet_, Circuit::ground(), inputB_);

      // NAND1: NAND(A, B) -> n1Out
      stampPmos(mna, vddNet_, inANet_, n1Out_, prevV, pmosParams_);
      stampPmos(mna, vddNet_, inBNet_, n1Out_, prevV, pmosParams_);
      stampNmos(mna, n1Out_, inANet_, n1Mid_, prevV, nmosParams_);
      stampNmos(mna, n1Mid_, inBNet_, Circuit::ground(), prevV, nmosParams_);

      // NAND2: NAND(A, N1) -> n2Out
      stampPmos(mna, vddNet_, inANet_, n2Out_, prevV, pmosParams_);
      stampPmos(mna, vddNet_, n1Out_, n2Out_, prevV, pmosParams_);
      stampNmos(mna, n2Out_, inANet_, n2Mid_, prevV, nmosParams_);
      stampNmos(mna, n2Mid_, n1Out_, Circuit::ground(), prevV, nmosParams_);

      // NAND3: NAND(B, N1) -> n3Out
      stampPmos(mna, vddNet_, inBNet_, n3Out_, prevV, pmosParams_);
      stampPmos(mna, vddNet_, n1Out_, n3Out_, prevV, pmosParams_);
      stampNmos(mna, n3Out_, inBNet_, n3Mid_, prevV, nmosParams_);
      stampNmos(mna, n3Mid_, n1Out_, Circuit::ground(), prevV, nmosParams_);

      // NAND4: NAND(N2, N3) -> out
      stampPmos(mna, vddNet_, n2Out_, outNet_, prevV, pmosParams_);
      stampPmos(mna, vddNet_, n3Out_, outNet_, prevV, pmosParams_);
      stampNmos(mna, outNet_, n2Out_, n4Mid_, prevV, nmosParams_);
      stampNmos(mna, n4Mid_, n3Out_, Circuit::ground(), prevV, nmosParams_);

      // GMIN shunts on all floating nodes
      mna.addConductance(n1Out_, Circuit::ground(), GMIN);
      mna.addConductance(n1Mid_, Circuit::ground(), GMIN);
      mna.addConductance(n2Out_, Circuit::ground(), GMIN);
      mna.addConductance(n2Mid_, Circuit::ground(), GMIN);
      mna.addConductance(n3Out_, Circuit::ground(), GMIN);
      mna.addConductance(n3Mid_, Circuit::ground(), GMIN);
      mna.addConductance(outNet_, Circuit::ground(), GMIN);
      mna.addConductance(n4Mid_, Circuit::ground(), GMIN);
    });
  }

  /* ----------------------------- API ----------------------------- */

  void build() { circuit_.build(); }

  void setInputs(double vinA, double vinB) noexcept {
    inputA_ = vinA;
    inputB_ = vinB;
  }

  [[nodiscard]] double computeDC() {
    TransientState state;
    solveNRConverge(circuit_, state, circuit_.netCount());
    return (outNet_ < state.nodeVoltages.size()) ? state.nodeVoltages[outNet_] : 0.0;
  }

  [[nodiscard]] NetID outputNet() const noexcept { return outNet_; }
  [[nodiscard]] double vdd() const noexcept { return vdd_; }

private:
  double vdd_;
  MosfetLevel1Params nmosParams_;
  MosfetLevel1Params pmosParams_;
  double inputA_ = 0.0;
  double inputB_ = 0.0;
  Circuit circuit_;
  NetID vddNet_ = 0;
  NetID inANet_ = 0;
  NetID inBNet_ = 0;
  NetID n1Out_ = 0;
  NetID n1Mid_ = 0;
  NetID n2Out_ = 0;
  NetID n2Mid_ = 0;
  NetID n3Out_ = 0;
  NetID n3Mid_ = 0;
  NetID outNet_ = 0;
  NetID n4Mid_ = 0;
};

/* ----------------------------- CmosXnorCircuit ----------------------------- */

/**
 * @brief Circuit-level CMOS XNOR gate -- 18 MOSFETs (XOR + NOT buffer).
 *
 * Topology: XOR(A,B) feeding an inverter to produce XNOR(A,B).
 *
 *   A,B --> [XOR (4 NANDs)] --xorOut--> [NOT] --> OUT
 *
 * @note NOT RT-safe.
 */
struct CmosXnorCircuit {

  /* ----------------------------- Construction ----------------------------- */

  CmosXnorCircuit(double vdd, double /*W*/, double /*L*/, const MosfetLevel1Params& nmosParams,
                  const MosfetLevel1Params& pmosParams) noexcept
      : vdd_(vdd), nmosParams_(nmosParams), pmosParams_(pmosParams) {
    vddNet_ = circuit_.addNet("VDD").id;
    inANet_ = circuit_.addNet("INA").id;
    inBNet_ = circuit_.addNet("INB").id;
    // NAND1 internals
    n1Out_ = circuit_.addNet("N1_OUT").id;
    n1Mid_ = circuit_.addNet("N1_MID").id;
    // NAND2 internals
    n2Out_ = circuit_.addNet("N2_OUT").id;
    n2Mid_ = circuit_.addNet("N2_MID").id;
    // NAND3 internals
    n3Out_ = circuit_.addNet("N3_OUT").id;
    n3Mid_ = circuit_.addNet("N3_MID").id;
    // NAND4 (XOR output) internals
    xorOutNet_ = circuit_.addNet("XOR_OUT").id;
    n4Mid_ = circuit_.addNet("N4_MID").id;
    // NOT buffer output
    outNet_ = circuit_.addNet("OUT").id;

    circuit_.addStamp([this](MnaSystem& mna, double, const std::vector<double>& prevV) {
      mna.addVoltageSource(vddNet_, Circuit::ground(), vdd_);
      mna.addVoltageSource(inANet_, Circuit::ground(), inputA_);
      mna.addVoltageSource(inBNet_, Circuit::ground(), inputB_);

      // NAND1: NAND(A, B) -> n1Out
      stampPmos(mna, vddNet_, inANet_, n1Out_, prevV, pmosParams_);
      stampPmos(mna, vddNet_, inBNet_, n1Out_, prevV, pmosParams_);
      stampNmos(mna, n1Out_, inANet_, n1Mid_, prevV, nmosParams_);
      stampNmos(mna, n1Mid_, inBNet_, Circuit::ground(), prevV, nmosParams_);

      // NAND2: NAND(A, N1) -> n2Out
      stampPmos(mna, vddNet_, inANet_, n2Out_, prevV, pmosParams_);
      stampPmos(mna, vddNet_, n1Out_, n2Out_, prevV, pmosParams_);
      stampNmos(mna, n2Out_, inANet_, n2Mid_, prevV, nmosParams_);
      stampNmos(mna, n2Mid_, n1Out_, Circuit::ground(), prevV, nmosParams_);

      // NAND3: NAND(B, N1) -> n3Out
      stampPmos(mna, vddNet_, inBNet_, n3Out_, prevV, pmosParams_);
      stampPmos(mna, vddNet_, n1Out_, n3Out_, prevV, pmosParams_);
      stampNmos(mna, n3Out_, inBNet_, n3Mid_, prevV, nmosParams_);
      stampNmos(mna, n3Mid_, n1Out_, Circuit::ground(), prevV, nmosParams_);

      // NAND4: NAND(N2, N3) -> xorOut
      stampPmos(mna, vddNet_, n2Out_, xorOutNet_, prevV, pmosParams_);
      stampPmos(mna, vddNet_, n3Out_, xorOutNet_, prevV, pmosParams_);
      stampNmos(mna, xorOutNet_, n2Out_, n4Mid_, prevV, nmosParams_);
      stampNmos(mna, n4Mid_, n3Out_, Circuit::ground(), prevV, nmosParams_);

      // NOT buffer: invert XOR output
      stampPmos(mna, vddNet_, xorOutNet_, outNet_, prevV, pmosParams_);
      stampNmos(mna, outNet_, xorOutNet_, Circuit::ground(), prevV, nmosParams_);

      // GMIN shunts on all floating nodes
      mna.addConductance(n1Out_, Circuit::ground(), GMIN);
      mna.addConductance(n1Mid_, Circuit::ground(), GMIN);
      mna.addConductance(n2Out_, Circuit::ground(), GMIN);
      mna.addConductance(n2Mid_, Circuit::ground(), GMIN);
      mna.addConductance(n3Out_, Circuit::ground(), GMIN);
      mna.addConductance(n3Mid_, Circuit::ground(), GMIN);
      mna.addConductance(xorOutNet_, Circuit::ground(), GMIN);
      mna.addConductance(n4Mid_, Circuit::ground(), GMIN);
      mna.addConductance(outNet_, Circuit::ground(), GMIN);
    });
  }

  /* ----------------------------- API ----------------------------- */

  void build() { circuit_.build(); }

  void setInputs(double vinA, double vinB) noexcept {
    inputA_ = vinA;
    inputB_ = vinB;
  }

  [[nodiscard]] double computeDC() {
    TransientState state;
    solveNRConverge(circuit_, state, circuit_.netCount());
    return (outNet_ < state.nodeVoltages.size()) ? state.nodeVoltages[outNet_] : 0.0;
  }

  [[nodiscard]] NetID outputNet() const noexcept { return outNet_; }
  [[nodiscard]] double vdd() const noexcept { return vdd_; }

private:
  double vdd_;
  MosfetLevel1Params nmosParams_;
  MosfetLevel1Params pmosParams_;
  double inputA_ = 0.0;
  double inputB_ = 0.0;
  Circuit circuit_;
  NetID vddNet_ = 0;
  NetID inANet_ = 0;
  NetID inBNet_ = 0;
  NetID n1Out_ = 0;
  NetID n1Mid_ = 0;
  NetID n2Out_ = 0;
  NetID n2Mid_ = 0;
  NetID n3Out_ = 0;
  NetID n3Mid_ = 0;
  NetID xorOutNet_ = 0;
  NetID n4Mid_ = 0;
  NetID outNet_ = 0;
};

} // namespace sim::electronics::topologies::gates

#endif // APEX_CMOSGATECIRCUITS_HPP
