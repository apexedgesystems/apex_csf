#ifndef APEX_SIM_ELECTRONICS_CPU_INTEL4004_INTEL4004GRIDLEVEL1_HPP
#define APEX_SIM_ELECTRONICS_CPU_INTEL4004_INTEL4004GRIDLEVEL1_HPP
/**
 * @file Intel4004GridLevel1.hpp
 * @brief Intel 4004 circuit with MOSFET Level 1 (Shichman-Hodges) device model.
 *
 * Replaces the binary switch MOSFET model in Intel4004Grid with the Level 1
 * analog model. Three device types:
 *
 * 1. Enhancement-mode logic (isLoad=false, isDiodeLoad=false):
 *    Standard Shichman-Hodges with Vth > 0. ON when gate is LOW.
 *
 * 2. Enhancement-mode load (isLoad=true, isDiodeLoad=false):
 *    Same equations but weaker Kp (by LOAD_RATIO). Conditional pull-ups.
 *
 * 3. Depletion-mode load (isDiodeLoad=true):
 *    Shichman-Hodges with Vth < 0 (negative threshold). Always ON even at
 *    VSG=0, modeling real depletion-mode PMOS devices. This is physically
 *    correct: depletion devices have a built-in channel that conducts at
 *    zero gate-source voltage. Conductance varies with bias (not fixed).
 *
 * The key benefit over binary switch: proper Newton-Raphson linearization
 * stamps gm (transconductance) into the MNA matrix, allowing gate voltage
 * changes to propagate through the circuit in a single solve step. This
 * enables the timing generator ring counter to bootstrap and provides
 * physically accurate charge retention on dynamic PMOS nodes.
 *
 * @note NOT RT-safe: initialization allocates. Stamp functions are RT-safe.
 */

#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1.hpp"
#include "src/sim/electronics/intel4004/grid/inc/Intel4004Components.hpp"
#include "src/sim/electronics/intel4004/grid/inc/Intel4004Grid.hpp"

#include <algorithm>
#include <functional>
#include <unordered_set>

namespace sim::electronics::intel4004 {

using devices::nonlinear::MosfetLevel1;
using devices::nonlinear::MosfetLevel1Params;

/* ----------------------------- Intel4004GridLevel1 ----------------------------- */

/**
 * @brief Intel 4004 circuit with Level 1 MOSFET device model.
 *
 * Inherits all infrastructure from Intel4004Grid (net allocation, simulation
 * loop, readback, external IO) and replaces only the transistor stamp function
 * with Shichman-Hodges analog I-V curves.
 *
 * Usage:
 * @code
 * Intel4004GridLevel1 grid;
 * auto circuit = grid.buildCircuit(netlist);
 * grid.enableSparseModeLevel1(circuit);  // <-- Level 1 stamps
 * auto state = grid.simulate(circuit, rom, romSize, numBytes);
 * @endcode
 */
struct Intel4004GridLevel1 : Intel4004Grid {

  /* ----------------------------- Constants ----------------------------- */

  /// Minimum conductance for numerical stability in cutoff region.
  static constexpr double G_MIN = 1e-12;

  /// GMIN: minimum conductance from every node to ground.
  /// Standard SPICE convergence aid. Prevents floating nodes and ensures
  /// the MNA matrix is non-singular during NR iteration.
  double gminTransient_ = 1e-9; ///< GMIN for transient (can be ramped during warmup).

  /// GMIN tier for "actively driven" nets (NOR outputs whose depletion
  /// load provides an algebraic constraint, clock signals forced via NR
  /// callback). 0 = use gminTransient_ uniformly. When set, this is
  /// applied INSTEAD of gminTransient_ on driven nets, while everything
  /// else (floating storage, pass-gate intermediates) gets gminTransient_.
  ///
  /// Use case: L2 with overlay off needs strong gminTransient_ (~1e-3)
  /// to algebraically anchor floating nets, but NOR outputs would have
  /// their logic levels distorted by 1e-3. Setting gminDriven_ = 1e-9
  /// keeps them clean.
  double gminDriven_ = 0.0;

  /// Parasitic capacitance for Level 1 model.
  static constexpr double CPARA_L1 = 10e-15;

  /* ----------------------------- Calibrated Level 1 Parameters ----------------------------- */

  /// Process parameters from Intel 4004 10-micron PMOS process (1971).
  /// W/L ratios from Monte Carlo calibration (score 0.108).
  /// Derived from GPU-accelerated Monte Carlo calibration against ngspice.
  static constexpr double KP_PROCESS = 5e-3; ///< Kp scaled for VDD=5V operation.
  static constexpr double VTH_ENH = 1.17;    ///< Enhancement threshold (V).
  static constexpr double VTH_DEP = -0.17;   ///< Depletion threshold (V).
  static constexpr double LAMBDA = 0.03;     ///< Channel-length modulation.

  /// Calibrated W/L ratios by transistor role.
  static constexpr double WL_DEPLETION_LOAD = 0.10;
  static constexpr double WL_DEPLETION_CASCADED = 0.14;
  static constexpr double WL_ENHANCEMENT_LOGIC = 3.23;
  static constexpr double WL_ENHANCEMENT_DRIVER = 5.77;
  static constexpr double WL_PASS_GATE_CLOCK = 1.41;
  static constexpr double WL_PASS_GATE_DATA = 1.10;
  static constexpr double WL_OUTPUT_DRIVER = 3.13;

  /// Per-transistor effective Kp (computed during buildCircuit from W/L bins).
  mutable std::vector<double> transistorKp_;

  /// Same-VTO mode: all transistors use same Vth but calibrated Kp per bin.
  /// Required for the working ACC=5 component hybrid configuration.
  bool sameVtoMode_ = true;
  /// Resistive load mode: depletion loads stamped as fixed conductance (G_LOAD).
  /// Required for the working ACC=5 component hybrid configuration.
  bool resistiveLoads_ = true;

  /// Pin DYNAMIC_STORAGE nets to clean rails after the transistor stamps.
  /// L1 default = hard voltage source (the 15% behavioral fraction).
  /// L2 = soft Norton anchor at `latchOverlayConductance_`, weak enough
  ///      that BSIM3 dominates the operating point but strong enough to
  ///      suppress spurious bistable flips from clock-edge transients.
  bool applyBehavioralLatchOverlay_ = true;

  /// Execute LDM/ADD/SUB/etc. instructions behaviorally at X3 of each
  /// machine cycle, writing the result directly to ACC node voltages
  /// (bypassing physics). L1 default = true (the digital execution path
  /// the multi-instruction tests rely on). L2 sets this false to test
  /// pure-physics multi-instruction: the analog data-bus -> OPA -> ACC
  /// transistor connections must propagate the value end-to-end.
  bool applyBehavioralX3_ = true;

  /// Force the M1.CLK2 sampled-data nodes (N1008..N1011 in the Lajos
  /// netlist) directly to the D-bit values, bypassing the chip-internal
  /// data-bus drivers that fight external forcing of D0..D3 during M1.
  /// L1 default = false (legacy behavior). L2 sets true when probing
  /// pure-physics multi-instruction: gives the decode chain correct
  /// inputs even when bus tri-state isn't yet working from physics.
  bool forceM1SampledData_ = false;

  /// Clamp NR iterates to [V_NR_LO, V_NR_HI] after each NR step.
  /// This is a "post-iteration limiter" -- a numerical convergence aid
  /// distinct from GMIN: it adds zero current to the circuit but
  /// algebraically forces nodes back into a sensible range when the
  /// linear solver produces unbounded steps. Enabling this lets us
  /// drop gminTransient_ to weak (1e-9) without reintroducing the
  /// ±100s-of-volts NR pathology, freeing pass-transistor drive on
  /// pass-driven nets like ~OPR.x in the decode chain.
  bool clampNrIterates_ = false;
  static constexpr double V_NR_LO = -1.0;
  static constexpr double V_NR_HI = 6.0;

  /// Conductance of the soft Norton anchor when overlay is enabled.
  /// 0.0 = hard voltage source (default; preserves L1 behavioral pinning).
  /// > 0 = Norton equivalent: addConductance(net, 0, G) + addCurrent(net, 0, G*V).
  ///
  /// Numerical interpretation: at conductance G, the anchor pulls a node
  /// toward `storedV` with current G*(storedV - V_node). When G is much
  /// smaller than the BSIM3 stamp's gm at the operating point, the anchor
  /// is dominated by physics; when G is much larger, the node is pinned.
  /// The MC-calibrated sweet spot is the smallest G where NR converges
  /// throughout the warmup -> program transition.
  double latchOverlayConductance_ = 0.0;

  /// Compute per-transistor Kp from calibrated W/L bins.
  void computeTransistorKp() const {
    transistorKp_.resize(transistors_.size());

    // Build timing net set for pass gate classification
    std::unordered_set<mna::NetID> timingNets;
    for (auto& [name, id] : netMap_) {
      if (name == "CLK1" || name == "CLK2" || name == "SYNC" ||
          name.find("CLK") != std::string::npos) {
        timingNets.insert(id);
      }
    }

    std::unordered_set<mna::NetID> padNets;
    for (auto& [name, id] : netMap_) {
      if (name == "D0" || name == "D1" || name == "D2" || name == "D3" ||
          name.find("PAD") != std::string::npos) {
        padNets.insert(id);
      }
    }

    for (std::size_t i = 0; i < transistors_.size(); ++i) {
      const auto& t = transistors_[i];
      double wl;

      if (t.isDiodeLoad) {
        wl = WL_DEPLETION_LOAD;
      } else if (t.isLoad && !t.isDiodeLoad) {
        wl = WL_DEPLETION_CASCADED;
      } else if (padNets.count(t.drain) || padNets.count(t.source)) {
        wl = WL_OUTPUT_DRIVER;
      } else if (timingNets.count(t.gate)) {
        wl = WL_PASS_GATE_CLOCK;
      } else {
        wl = WL_ENHANCEMENT_LOGIC;
      }

      transistorKp_[i] = KP_PROCESS * wl;
    }
  }

  /* ----------------------------- NR State ----------------------------- */

  /// Per-transistor previous VSG for fetlim voltage limiting.
  /// Sized to transistors_.size() on first stamp call.
  mutable std::vector<double> prevVsg_;
  mutable std::vector<double> prevVsd_;

  /// Component-mode: use per-type stamp strategies instead of global parameters.
  /// NOR gates use Level 1 physics, dynamic storage uses behavioral latches,
  /// pass gates use binary switch.
  bool componentMode_ = true;
  mutable std::vector<ComponentType> componentTypes_;

  /// Stored latch values for dynamic storage nets (behavioral sample-and-hold).
  /// Key = net ID, value = stored voltage (0V or VDD).
  mutable std::unordered_map<mna::NetID, double> latchValues_;

  /// Initial ACC value to seed into behavioral latch on first stamp call.
  /// Set before traceExecuteByte to override binary switch warmup result.
  /// -1 = use warmup readback (default).
  int initialAcc_ = -1;

  /**
   * @brief Seed behavioral latches from a behavioral CPU state.
   *
   * Copies ACC, carry, and all 16 index registers from the behavioral CPU
   * into latchValues_. Call after enableSparseModeLevel1 and before
   * traceExecuteByte to ensure the L1 circuit starts from the correct state.
   */
  void seedFromBehavioral(const Intel4004Grid& /*unused*/, std::uint8_t acc, bool carry,
                          const std::uint8_t* regs16) {
    // Directly overwrite ACC, carry, and register values in latchValues_.
    // Called after simulateLevel1 warmup, so latchValues_ is already populated.
    for (int b = 0; b < 4; ++b) {
      if (accNets_[b] > 0)
        latchValues_[accNets_[b]] = (acc & (1 << b)) ? 0.0 : VDD_VOLTAGE;
    }
    if (cyNet_ > 0)
      latchValues_[cyNet_] = carry ? 0.0 : VDD_VOLTAGE;
    for (int r = 0; r < 16; ++r) {
      for (int b = 0; b < 4; ++b) {
        auto net = regNets_[r][b];
        if (net > 0)
          latchValues_[net] = (regs16[r] & (1 << b)) ? 0.0 : VDD_VOLTAGE;
      }
    }
    initialAcc_ = acc;
  }

  /// Previous TIMESTEP voltages for companion model current sources.
  /// Must be constant during NR iteration (cap ieq uses this, not the NR iterate).
  /// Captured on the first stamp callback call of each timestep (stampCallCount_==0).
  mutable std::vector<double> prevTimestepV_;
  mutable std::size_t stampCallCount_ = 0;

  /* ----------------------------- Tracing API ----------------------------- */

  /**
   * @brief Execute one byte with per-state callback for signal tracing.
   *
   * Runs 8 machine states x 2 clock phases for a single ROM byte.
   * Calls the callback after each clock phase with the current state.
   *
   * @param circuit Circuit with Level 1 stamps configured.
   * @param state Transient state (modified in-place).
   * @param romByte The byte being executed.
   * @param onPhase Callback(machineState, clockPhase, nodeVoltages).
   *                clockPhase: 0=CLK1, 1=CLK2.
   */
  void traceExecuteByte(
      circuit::Circuit& circuit, sim::electronics::transient::TransientState& state,
      std::uint8_t romByte,
      std::function<void(std::uint8_t ms, int clkPhase, const std::vector<double>&)> onPhase) {
    const double PHASE_DURATION = 1e-6 / 4.0;
    const double SUB_STEP = PHASE_DURATION / 10.0;
    std::vector<double> prevSubV(state.nodeVoltages.size());

    // Cache net IDs for behavioral forcing.
    auto scNetLocal = findNet("SC");
    auto scM12Clk2Net = findNet("SC&M12&CLK2");
    auto scM22Clk2Net = findNet("SC&M22&CLK2");
    auto opaIbNet = findNet("OPA-IB");

    // Force behavioral signals into a voltage vector. driveNet (G_DRIVER
    // conductance) is overwhelmed by Level 1 stamp loads on timing nets.
    // All behavioral signals must be forced by direct voltage override.
    // Behavioral signals must be forced by voltage override (G_DRIVER overwhelmed).
    auto forceBehavioral = [&](std::vector<double>& v) {
      const auto N = v.size();
      auto fv = [&](mna::NetID net, double val) {
        if (net > 0 && net < N)
          v[net] = val;
      };
      fv(clk1Net_, clk1High_ ? VDD_VOLTAGE : 0.0);
      fv(clk2Net_, clk2High_ ? VDD_VOLTAGE : 0.0);
      fv(scNetLocal, 0.0);
      for (std::size_t i = 0; i < 8; ++i) {
        fv(timingNets_[i], (machineState_ == i) ? 0.0 : VDD_VOLTAGE);
      }
      fv(syncNet_, (machineState_ <= 2) ? VDD_VOLTAGE : 0.0);
      fv(scM12Clk2Net, ((machineState_ == 3) && !clk2High_) ? 0.0 : VDD_VOLTAGE);
      fv(scM22Clk2Net, ((machineState_ == 4) && !clk2High_) ? 0.0 : VDD_VOLTAGE);
      fv(opaIbNet, (machineState_ == 4) ? 0.0 : VDD_VOLTAGE);

      if (dataBusDriving_) {
        for (int b = 0; b < 4; ++b) {
          bool bitSet = (dataBusDrive_ >> b) & 1;
          fv(dataBusNets_[b], bitSet ? 0.0 : VDD_VOLTAGE);
        }
      }

      // Optionally also force the M1.CLK2 sampled-data nodes directly,
      // bypassing the data-bus contention with chip-internal drivers.
      // Per Kintli reverse-engineering: N1011=D0, N1010=D1, N1009=D2,
      // N1008=D3 sampled when SC&M12&CLK2 fires.
      if (forceM1SampledData_ && machineState_ == 3 && dataBusDriving_ && !clk2High_) {
        const char* nNames[] = {"N1011", "N1010", "N1009", "N1008"}; // bit 0..3
        for (int b = 0; b < 4; ++b) {
          auto id = findNet(nNames[b]);
          if (id == 0) continue;
          bool bitSet = (dataBusDrive_ >> b) & 1;
          fv(id, bitSet ? 0.0 : VDD_VOLTAGE);
        }
      }

      // Behavioral latch updates: sample data bus into OPA during M2,
      // and transfer OPA to ACC during X3 for LDM instructions.
      if (!latchValues_.empty()) {
        // M1: latch high nibble from data bus into OPR (opcode register)
        if (machineState_ == 3 && dataBusDriving_) {
          mna::NetID oprNets[] = {findNet("OPR.0"), findNet("OPR.1"),
                                  findNet("OPR.2"), findNet("OPR.3")};
          for (int b = 0; b < 4; ++b) {
            if (oprNets[b] > 0) {
              double val = ((dataBusDrive_ >> b) & 1) ? 0.0 : VDD_VOLTAGE;
              latchValues_[oprNets[b]] = val; // insert or overwrite
              fv(oprNets[b], val);
            }
          }
        }
        // M2: latch low nibble from data bus into OPA
        if (machineState_ == 4 && dataBusDriving_) {
          mna::NetID opaNets[] = {findNet("OPA.0"), findNet("OPA.1"),
                                  findNet("OPA.2"), findNet("OPA.3")};
          for (int b = 0; b < 4; ++b) {
            if (opaNets[b] > 0) {
              double val = ((dataBusDrive_ >> b) & 1) ? 0.0 : VDD_VOLTAGE;
              latchValues_[opaNets[b]] = val;
              fv(opaNets[b], val);
            }
          }
        }
        // X3 instruction execution moved to executeInstruction() called
        // after X3 sub-steps complete (not inside NR callback).
        if (false) { // disabled -- see executeInstruction() below
          // Read 4-bit value from latch
          auto readLatch4 = [&](const char* n0, const char* n1,
                                const char* n2, const char* n3) -> std::uint8_t {
            mna::NetID nets[] = {findNet(n0), findNet(n1), findNet(n2), findNet(n3)};
            std::uint8_t val = 0;
            for (int b = 0; b < 4; ++b) {
              if (nets[b] > 0 && latchValues_.count(nets[b])) {
                if (latchValues_[nets[b]] < VDD_VOLTAGE * 0.5) val |= (1 << b);
              }
            }
            return val;
          };
          // Write 4-bit value to ACC latches
          auto writeAcc = [&](std::uint8_t val) {
            for (int b = 0; b < 4; ++b) {
              if (accNets_[b] > 0 && latchValues_.count(accNets_[b])) {
                double vv = (val & (1 << b)) ? 0.0 : VDD_VOLTAGE;
                latchValues_[accNets_[b]] = vv;
                fv(accNets_[b], vv);
              }
            }
          };
          // Read/write carry
          auto cyNet = findNet("CY");
          auto readCy = [&]() -> bool {
            if (cyNet > 0 && latchValues_.count(cyNet))
              return latchValues_[cyNet] < VDD_VOLTAGE * 0.5;
            return false;
          };
          auto writeCy = [&](bool cy) {
            if (cyNet > 0 && latchValues_.count(cyNet)) {
              latchValues_[cyNet] = cy ? 0.0 : VDD_VOLTAGE;
              fv(cyNet, latchValues_[cyNet]);
            }
          };

          std::uint8_t opr = readLatch4("OPR.0", "OPR.1", "OPR.2", "OPR.3");
          std::uint8_t opa = readLatch4("OPA.0", "OPA.1", "OPA.2", "OPA.3");
          std::uint8_t acc = readLatch4("ACC.0", "ACC.1", "ACC.2", "ACC.3");
          bool cy = readCy();

          // Instruction dispatch based on OPR (opcode high nibble)
          switch (opr) {
          case 0x8: { // ADD Rr: ACC = ACC + register[Rr] + carry
            std::uint8_t regVal = 0;
            if (opa < 16) {
              for (int b = 0; b < 4; ++b) {
                auto net = regNets_[opa][b];
                if (net > 0 && latchValues_.count(net))
                  if (latchValues_[net] < VDD_VOLTAGE * 0.5) regVal |= (1 << b);
              }
            }
            unsigned sum = acc + regVal + (cy ? 1 : 0);
            writeAcc(sum & 0xF);
            writeCy(sum > 0xF);
            break;
          }
          case 0x9: { // SUB Rr: ACC = ACC - register[Rr] - borrow
            std::uint8_t regVal = 0;
            if (opa < 16) {
              for (int b = 0; b < 4; ++b) {
                auto net = regNets_[opa][b];
                if (net > 0 && latchValues_.count(net))
                  if (latchValues_[net] < VDD_VOLTAGE * 0.5) regVal |= (1 << b);
              }
            }
            unsigned diff = acc + (~regVal & 0xF) + (cy ? 1 : 0);
            writeAcc(diff & 0xF);
            writeCy(diff > 0xF);
            break;
          }
          case 0xA: { // LD Rr: ACC = register[Rr]
            // Read register value from latch values (reg nets)
            std::uint8_t regVal = 0;
            if (opa < 16) {
              for (int b = 0; b < 4; ++b) {
                auto net = regNets_[opa][b];
                if (net > 0 && latchValues_.count(net)) {
                  if (latchValues_[net] < VDD_VOLTAGE * 0.5) regVal |= (1 << b);
                }
              }
            }
            writeAcc(regVal);
            break;
          }
          case 0xB: { // XCH Rr: swap ACC and Rr (ACC part only)
            writeAcc(opa);
            break;
          }
          case 0xD: // LDM d: ACC = immediate
            writeAcc(opa);
            break;
          case 0xF: // Accumulator group: decode from OPA
            switch (opa) {
            case 0x0: // CLB: clear ACC and carry
              writeAcc(0);
              writeCy(false);
              break;
            case 0x1: // CLC: clear carry
              writeCy(false);
              break;
            case 0x2: { // IAC: ACC = ACC + 1
              unsigned sum = acc + 1;
              writeAcc(sum & 0xF);
              writeCy(sum > 0xF);
              break;
            }
            case 0x3: // CMC: complement carry
              writeCy(!cy);
              break;
            case 0x4: // CMA: complement ACC
              writeAcc(~acc & 0xF);
              break;
            case 0x5: { // RAL: rotate left through carry
              unsigned rot = (acc << 1) | (cy ? 1 : 0);
              writeAcc(rot & 0xF);
              writeCy((rot >> 4) & 1);
              break;
            }
            case 0x6: { // RAR: rotate right through carry
              unsigned rot = acc | (cy ? 0x10 : 0);
              writeAcc((rot >> 1) & 0xF);
              writeCy(rot & 1);
              break;
            }
            case 0x7: // TCC: ACC = carry, clear carry
              writeAcc(cy ? 1 : 0);
              writeCy(false);
              break;
            case 0x9: // TCS: ACC = carry?10:9, clear carry
              writeAcc(cy ? 10 : 9);
              writeCy(false);
              break;
            case 0xA: // STC: set carry
              writeCy(true);
              break;
            default:
              break; // DAA, KBP, DCL: not yet implemented
            }
            break;
          default:
            break; // NOP, JCN, FIM, JUN, JMS, INC, ISZ, BBL, I/O: no ACC write
          }
        }
        // Force all latch values
        for (auto& [net, storedV] : latchValues_) {
          fv(net, storedV);
        }
      }

      // Optional plug-in: post-iteration voltage clamp.
      if (clampNrIterates_) {
        for (std::size_t i = 1; i < v.size(); ++i) {
          if (i == vdd_) continue;
          if (v[i] < V_NR_LO) v[i] = V_NR_LO;
          else if (v[i] > V_NR_HI) v[i] = V_NR_HI;
        }
      }
    };

    // Install nrLimitCallback: force behavioral signals after EACH NR
    // iteration so the solver sees correct timing throughout convergence.
    circuit.solver().setNrLimitCallback(
        [&](std::vector<double>& newV, const std::vector<double>& /*prevIterV*/) {
          forceBehavioral(newV);
        });

    for (std::uint8_t ms = 0; ms < 8; ++ms) {
      machineState_ = ms;
      if (ms == 3) {
        dataBusDriving_ = true;
        dataBusDrive_ = (romByte >> 4) & 0xF;
      } else if (ms == 4) {
        dataBusDriving_ = true;
        dataBusDrive_ = romByte & 0xF;
      } else {
        dataBusDriving_ = false;
      }

      // CLK1 phase
      clk1High_ = false;
      clk2High_ = true;
      circuit.solver().invalidateCache();
      runSubSteps(circuit, state, 10, SUB_STEP, false, 0, state.nodeVoltages.size(), prevSubV);
      forceBehavioral(state.nodeVoltages);
      if (onPhase)
        onPhase(ms, 0, state.nodeVoltages);

      // Dead zone (both HIGH)
      clk1High_ = true;
      clk2High_ = true;
      circuit.solver().invalidateCache();
      runSubSteps(circuit, state, 10, SUB_STEP, false, 0, state.nodeVoltages.size(), prevSubV);
      forceBehavioral(state.nodeVoltages);

      // CLK2 phase
      clk1High_ = true;
      clk2High_ = false;
      circuit.solver().invalidateCache();
      runSubSteps(circuit, state, 10, SUB_STEP, false, 0, state.nodeVoltages.size(), prevSubV);
      forceBehavioral(state.nodeVoltages);
      if (onPhase)
        onPhase(ms, 1, state.nodeVoltages);

      // Dead zone
      clk1High_ = true;
      clk2High_ = true;
      dataBusDriving_ = false;
      circuit.solver().invalidateCache();
      runSubSteps(circuit, state, 10, SUB_STEP, false, 0, state.nodeVoltages.size(), prevSubV);
      forceBehavioral(state.nodeVoltages);

      // After X3 phase: execute instruction behaviorally.
      // OPR was latched during M1, OPA during M2. Now decode and execute.
      if (ms == 7 && applyBehavioralX3_ && !latchValues_.empty()) {
        auto readLatch4 = [&](const char* n0, const char* n1,
                              const char* n2, const char* n3) -> std::uint8_t {
          mna::NetID nets[] = {findNet(n0), findNet(n1), findNet(n2), findNet(n3)};
          std::uint8_t val = 0;
          for (int b = 0; b < 4; ++b)
            if (nets[b] > 0 && latchValues_.count(nets[b]))
              if (latchValues_[nets[b]] < VDD_VOLTAGE * 0.5) val |= (1 << b);
          return val;
        };
        auto writeAcc = [&](std::uint8_t val) {
          for (int b = 0; b < 4; ++b)
            if (accNets_[b] > 0) {
              double vv = (val & (1 << b)) ? 0.0 : VDD_VOLTAGE;
              latchValues_[accNets_[b]] = vv;
              state.nodeVoltages[accNets_[b]] = vv;
            }
        };
        auto cyNet = findNet("CY");
        auto readCy = [&]() -> bool {
          return (cyNet > 0 && latchValues_.count(cyNet)) ? latchValues_[cyNet] < VDD_VOLTAGE * 0.5 : false;
        };
        auto writeCy = [&](bool cy) {
          if (cyNet > 0) { latchValues_[cyNet] = cy ? 0.0 : VDD_VOLTAGE; state.nodeVoltages[cyNet] = latchValues_[cyNet]; }
        };
        auto readReg = [&](std::uint8_t idx) -> std::uint8_t {
          std::uint8_t val = 0;
          if (idx < 16)
            for (int b = 0; b < 4; ++b) {
              auto net = regNets_[idx][b];
              if (net > 0 && latchValues_.count(net))
                if (latchValues_[net] < VDD_VOLTAGE * 0.5) val |= (1 << b);
            }
          return val;
        };

        std::uint8_t opr = readLatch4("OPR.0", "OPR.1", "OPR.2", "OPR.3");
        std::uint8_t opa = readLatch4("OPA.0", "OPA.1", "OPA.2", "OPA.3");
        std::uint8_t acc = readLatch4("ACC.0", "ACC.1", "ACC.2", "ACC.3");
        bool cy = readCy();

        switch (opr) {
        case 0x8: { unsigned s = acc + readReg(opa) + (cy?1:0); writeAcc(s&0xF); writeCy(s>0xF); break; }
        case 0x9: { auto r=readReg(opa); unsigned d=acc+(~r&0xF)+(cy?1:0); writeAcc(d&0xF); writeCy(d>0xF); break; }
        case 0xA: writeAcc(readReg(opa)); break;
        case 0xB: writeAcc(readReg(opa)); break; // XCH ACC part
        case 0xD: writeAcc(opa); break;
        case 0xF:
          switch (opa) {
          case 0x0: writeAcc(0); writeCy(false); break;
          case 0x1: writeCy(false); break;
          case 0x2: { unsigned s=acc+1; writeAcc(s&0xF); writeCy(s>0xF); break; }
          case 0x3: writeCy(!cy); break;
          case 0x4: writeAcc(~acc&0xF); break;
          case 0x5: { unsigned r=(acc<<1)|(cy?1:0); writeAcc(r&0xF); writeCy((r>>4)&1); break; }
          case 0x6: { unsigned r=acc|(cy?0x10:0); writeAcc((r>>1)&0xF); writeCy(r&1); break; }
          case 0x7: writeAcc(cy?1:0); writeCy(false); break;
          case 0x9: writeAcc(cy?10:9); writeCy(false); break;
          case 0xA: writeCy(true); break;
          default: break;
          }
          break;
        default: break;
        }
      }
    }
    ++bytesFetched_;
  }

  /**
   * @brief Force ACC node voltages to clean rails matching a logic value.
   *
   * Used by L2+ multi-instruction tests for byte-boundary state passing.
   * The logic value (0-15) maps to PMOS rails: LSB->ACC.0, etc.
   * Logic 1 -> 0V (LOW), logic 0 -> 5V (HIGH).
   */
  void forceAccLogic(std::vector<double>& v, std::uint8_t logicValue) {
    for (int b = 0; b < 4; ++b) {
      if (accNets_[b] > 0 && accNets_[b] < v.size()) {
        v[accNets_[b]] = (logicValue & (1 << b)) ? 0.0 : VDD_VOLTAGE;
        // Update behavioral latch state to match
        if (!latchValues_.empty()) {
          latchValues_[accNets_[b]] = (logicValue & (1 << b)) ? 0.0 : VDD_VOLTAGE;
        }
      }
    }
  }

  /* ----------------------------- API ----------------------------- */

  /**
   * @brief Enable sparse KLU solver with Level 1 MOSFET stamps.
   * @param circuit Configured Circuit from buildCircuit().
   *
   * Replaces the binary switch stamp callback with Level 1 analog stamps.
   * Must be called AFTER buildCircuit() but BEFORE simulate().
   */
  void enableSparseModeLevel1(circuit::Circuit& circuit) {
    circuit.solver().setStatefulStampCallbackSparse(
        [this](mna::MnaSystemSparse& mna, double /*time*/, const std::vector<double>& prevV) {
          // Cap companion IEQ = GEQ * V_{n-1} MUST use the previous timestep
          // voltage (constant during NR), NOT the NR iterate. If the NR iterate
          // diverges, using it for cap IEQ creates a positive feedback loop:
          //   diverged V -> large IEQ -> more divergence -> larger IEQ -> infinity
          //
          // The stamp callback count tracks NR iterations. On count 0 (first
          // call of a new timestep), capture prevV as the timestep voltage.
          // On subsequent calls (NR iterations 1+), use the captured value.
          // The count is reset externally via resetStampCount() between timesteps.
          if (prevTimestepV_.size() != prevV.size() || stampCallCount_ == 0) {
            prevTimestepV_ = prevV;
          }
          ++stampCallCount_;

          mna.addVoltageSource(vdd_, circuit::Circuit::ground(), VDD_VOLTAGE);
          stampTransistorsLevel1(mna, prevV); // MOSFETs linearize around NR iterate
          stampExternalIO(mna);
          stampParasiticCapsLevel1(mna, prevTimestepV_); // Caps use previous TIMESTEP (constant)
          stampGmin(mna, prevV.size());
          // (debug removed)
        });
    circuit.solver().setSparse(true);
    circuit.solver().setAlwaysReanalyze(true);

    // Pre-NR callback: update stepDt_, reset stamp count, and force clock
    // voltages in the NR initial iterate. CLK1/CLK2 are external inputs
    // that the solver cannot drive correctly. Forcing them here
    // ensures every NR iteration starts with correct clock values.
    // Force external signal voltages in the NR initial iterate.
    // CLK1/CLK2 are external chip inputs. SC is a ring counter node
    // whose precharge chain does not converge under Level 1 stamps.
    // Forcing these is physically justified: they are either external
    // or their behavior is deterministic from the instruction set.
    auto scNet = findNet("SC");
    circuit.solver().setNrPreBatchCallback([this, &circuit, scNet](double subDt) {
      stepDt_ = subDt;
      stampCallCount_ = 0;
      auto& prevV = circuit.solver().prevVoltages();
      if (clk1Net_ > 0 && clk1Net_ < prevV.size())
        prevV[clk1Net_] = clk1High_ ? VDD_VOLTAGE : 0.0;
      if (clk2Net_ > 0 && clk2Net_ < prevV.size())
        prevV[clk2Net_] = clk2High_ ? VDD_VOLTAGE : 0.0;
      if (scNet > 0 && scNet < prevV.size())
        prevV[scNet] = 0.0;
    });

    // Force behavioral signals after each NR iteration. SC is a ring counter
    // node that can't be computed by Level 1 stamps. CLK1/CLK2 are external
    // inputs. Forcing them in the NR result prevents the solve from overriding.
    {
      auto scN = findNet("SC");
      circuit.solver().setNrLimitCallback(
          [this, scN](std::vector<double>& newV, const std::vector<double>&) {
            if (clk1Net_ > 0 && clk1Net_ < newV.size())
              newV[clk1Net_] = clk1High_ ? VDD_VOLTAGE : 0.0;
            if (clk2Net_ > 0 && clk2Net_ < newV.size())
              newV[clk2Net_] = clk2High_ ? VDD_VOLTAGE : 0.0;
            if (scN > 0 && scN < newV.size())
              newV[scN] = 0.0; // LOW for single-cycle instructions
            // Optional plug-in: post-iteration voltage clamp. Adds zero
            // current but caps NR step pathology -- lets us run with
            // weak GMIN so pass-transistor drive isn't overpowered.
            if (clampNrIterates_) {
              for (std::size_t i = 1; i < newV.size(); ++i) {
                if (i == vdd_) continue;
                if (newV[i] < V_NR_LO) newV[i] = V_NR_LO;
                else if (newV[i] > V_NR_HI) newV[i] = V_NR_HI;
              }
            }
          });
    }
  }

  /**
   * @brief Two-phase simulation: binary switch warm-up + Level 1 execution.
   *
   * The Level 1 model's smooth I-V curves cannot bootstrap the 4004's dynamic
   * timing generator ring counter from all-zeros initial conditions. The binary
   * switch model's aggressive ON/OFF transitions successfully initialize the
   * ring counter through clock-driven transient warm-up.
   *
   * Phase 1: Run warmupBytes through binary switch stamps to establish correct
   *          node voltages for the timing generator and internal registers.
   * Phase 2: Switch to Level 1 stamps and run programBytes with physically
   *          accurate Shichman-Hodges I-V curves for charge retention.
   *
   * @param circuit Circuit from buildCircuit().
   * @param rom Full ROM (warm-up NOPs + program bytes).
   * @param romSize Size of ROM array.
   * @param warmupBytes Number of leading bytes simulated with binary switch.
   * @param programBytes Number of trailing bytes simulated with Level 1.
   * @param clockPeriod Time per machine state in seconds.
   * @param stepsPerPhase Sub-steps per clock phase.
   * @param convergenceThreshold Early termination threshold (0 = disabled).
   * @return Final TransientState with node voltages.
   *
   * @note NOT RT-safe: allocates.
   */
  sim::electronics::transient::TransientState
  simulateLevel1(circuit::Circuit& circuit, const std::uint8_t* rom, std::size_t romSize,
                 std::size_t warmupBytes, std::size_t programBytes, double clockPeriod = 1e-6,
                 std::size_t stepsPerPhase = 10, double convergenceThreshold = 0.01) {

    sim::electronics::transient::TransientState state;
    state.resize(circuit.netCount(), 0);

    const double PHASE_DURATION = clockPeriod / 4.0;
    const bool USE_CONVERGENCE = convergenceThreshold > 0.0;
    const std::size_t NET_COUNT = circuit.netCount();
    std::vector<double> prevSubV;
    if (USE_CONVERGENCE) {
      prevSubV.resize(NET_COUNT);
    }

    dataBusDriving_ = false;
    clk1High_ = true;
    clk2High_ = true;
    bytesFetched_ = 0;

    // Phase 1: Binary switch warm-up (stepsPerPhase=20).
    // The binary switch model needs 20 sub-steps per phase for correct signal
    // propagation through the dynamic timing generator ring counter.
    static constexpr std::size_t WARMUP_STEPS = 20;
    const double WARMUP_SUB_STEP = PHASE_DURATION / static_cast<double>(WARMUP_STEPS);

    enableSparseMode(circuit);
    circuit.solver().setCachedLU(false);

    runByteLoop(circuit, state, rom, romSize, 0, warmupBytes, WARMUP_SUB_STEP, WARMUP_STEPS,
                USE_CONVERGENCE, convergenceThreshold, prevSubV);

    // Phase 1.5: Map binary switch logic levels to Level 1 operating points.
    // Binary switch produces 0V/5V. Level 1 LOW nodes should be ~VOL (1.2V).
    // Without this mapping, the Level 1 NR sees 0V LOW nodes where the
    // enhancement PMOS has VSG=0V (deep cutoff, zero conductance), creating
    // a singular Jacobian. Mapping 0V -> VOL gives the NR a feasible starting
    // point where all MOSFETs have finite conductance.
    {
      // VOL = Vth_enh + |Vth_dep| * sqrt(WL_dep / WL_enh)
      constexpr double VOL = 1.2;
      constexpr double THRESHOLD = VDD_VOLTAGE / 2.0;
      for (std::size_t i = 1; i < state.nodeVoltages.size(); ++i) {
        if (i == vdd_)
          continue;
        double v = state.nodeVoltages[i];
        if (v < THRESHOLD && v < VOL) {
          state.nodeVoltages[i] = VOL; // Map LOW binary switch -> Level 1 VOL
        }
      }
    }

    // Phase 2: Level 1 analog execution.
    const double L1_SUB_STEP = PHASE_DURATION / static_cast<double>(stepsPerPhase);

    enableSparseModeLevel1(circuit);
    circuit.solver().invalidateCache();

    runByteLoop(circuit, state, rom, romSize, warmupBytes, programBytes, L1_SUB_STEP, stepsPerPhase,
                USE_CONVERGENCE, convergenceThreshold, prevSubV);

    circuit.solver().setCachedLU(false);
    return state;
  }

  /**
   * @brief Level 1 simulation from t=0 with behavioral timing injection.
   *
   * Eliminates the binary switch to Level 1 model transition that causes NR
   * divergence. Runs Level 1 stamps from the first timestep. Behavioral timing
   * injection drives the timing generator signals throughout.
   *
   * All nodes start at VDD/2 (standard NR initial guess for unknown circuits).
   * The NR solver finds the correct operating point during the first few
   * machine cycles (warm-up NOPs) using Level 1 physics from the start.
   *
   * @param circuit Circuit from buildCircuit().
   * @param rom Full ROM (warm-up NOPs + program bytes).
   * @param romSize Size of ROM array.
   * @param warmupBytes NOPs for circuit to reach steady state (Level 1 throughout).
   * @param programBytes Bytes to execute after warm-up.
   * @param clockPeriod Time per machine state in seconds.
   * @param stepsPerPhase Sub-steps per clock phase.
   * @param convergenceThreshold Early termination threshold (0 = disabled).
   * @return Final TransientState with node voltages.
   */
  sim::electronics::transient::TransientState
  simulateLevel1FromScratch(circuit::Circuit& circuit, const std::uint8_t* rom, std::size_t romSize,
                            std::size_t warmupBytes, std::size_t programBytes,
                            double clockPeriod = 1e-6, std::size_t stepsPerPhase = 10,
                            double convergenceThreshold = 0.01) {

    sim::electronics::transient::TransientState state;
    state.resize(circuit.netCount(), 0);

    const double PHASE_DURATION = clockPeriod / 4.0;
    const bool USE_CONVERGENCE = convergenceThreshold > 0.0;
    const std::size_t NET_COUNT = circuit.netCount();
    std::vector<double> prevSubV;
    if (USE_CONVERGENCE) {
      prevSubV.resize(NET_COUNT);
    }

    dataBusDriving_ = false;
    clk1High_ = true;
    clk2High_ = true;
    bytesFetched_ = 0;
    behavioralTiming_ = true; // Drive timing signals (was missing!)

    // Start from zero (like binary switch does). No DC init needed.
    // The 5V NR damping in TransientSolver keeps 87% of nodes bounded on
    // the first step. Remaining dynamic nodes settle over the
    // warmup NOP machine cycles via clocked refresh.
    for (std::size_t i = 1; i < NET_COUNT; ++i) {
      state.nodeVoltages[i] = (i == vdd_) ? VDD_VOLTAGE : 0.0;
    }

    const double L1_SUB_STEP = PHASE_DURATION / static_cast<double>(stepsPerPhase);

    enableSparseModeLevel1(circuit);
    circuit.solver().invalidateCache();

    // Transient GMIN ramping: start with large GMIN (1e-3) for first warmup
    // bytes, reduce by 10x per byte, until reaching target (1e-9).
    // This is GMIN stepping applied to the transient: each byte is a small
    // perturbation from the previous (clocks change but GMIN anchors nodes).
    // DC GMIN stepping prevents first-step divergence.
    {
      double gminSteps[] = {1e-3, 1e-4, 1e-5, 1e-6, 1e-7, 1e-8, 1e-9};
      std::size_t numGminSteps = sizeof(gminSteps) / sizeof(gminSteps[0]);
      std::size_t warmupPerGmin = std::max<std::size_t>(1, warmupBytes / numGminSteps);

      for (std::size_t g = 0; g < numGminSteps && bytesFetched_ < warmupBytes; ++g) {
        gminTransient_ = gminSteps[g];
        std::size_t bytesThisStep = std::min(warmupPerGmin, warmupBytes - bytesFetched_);
        runByteLoop(circuit, state, rom, romSize, bytesFetched_, bytesThisStep, L1_SUB_STEP,
                    stepsPerPhase, USE_CONVERGENCE, convergenceThreshold, prevSubV);
      }
      // Run remaining warmup at target GMIN
      if (bytesFetched_ < warmupBytes) {
        gminTransient_ = 1e-9;
        runByteLoop(circuit, state, rom, romSize, bytesFetched_, warmupBytes - bytesFetched_,
                    L1_SUB_STEP, stepsPerPhase, USE_CONVERGENCE, convergenceThreshold, prevSubV);
      }
    }

    // Program execution at target GMIN
    gminTransient_ = 1e-9;
    runByteLoop(circuit, state, rom, romSize, warmupBytes, programBytes, L1_SUB_STEP, stepsPerPhase,
                USE_CONVERGENCE, convergenceThreshold, prevSubV);

    circuit.solver().setCachedLU(false);
    return state;
  }

  /* ----------------------------- Level 1 Stamping ----------------------------- */

  /**
   * @brief Stamp the latch feedback core for a single DYNAMIC_STORAGE
   * transistor whose gate is NOT a NOR output net.
   *
   * L1 default: binary switch (the 15% behavioral fraction). Shichman-
   * Hodges cannot resolve this region because the depletion-load PMOS
   * NOR's `VOL = 1.20V > VTH_enh = 1.17V` (overdrive -30 mV).
   *
   * L2 (Intel4004GridLevel2) overrides this hook with BSIM3's smooth
   * Vgst_eff to capture moderate-inversion conduction --> 100% physics.
   *
   * Virtual is non-template; the only concrete MNA type used by the
   * solver callback is mna::MnaSystemSparse.
   */
  virtual void stampLatchFeedbackTransistor(mna::MnaSystemSparse& mna, std::size_t idx,
                                            const std::vector<double>& prevV) const {
    const auto& t = transistors_[idx];
    const auto& bp = bsParams_;
    double vs = std::max(prevV[t.drain], prevV[t.source]);
    double vgs = prevV[t.gate] - vs;
    double gds;
    if (vgs < -bp.vth - bp.subthMargin)
      gds = bp.gOn;
    else if (vgs > -bp.vth + bp.subthMargin)
      gds = bp.gOff;
    else
      gds = bp.gSubth;
    mna.addConductance(t.drain, t.source, gds);
  }

  /**
   * @brief Stamp all PMOS transistors using Newton-Raphson linearized Level 1.
   *
   * For each transistor, stamps three components:
   *   1. Gate VCCS: gm coupling (gate voltage -> drain current)
   *   2. Output conductance: gds between drain and source
   *   3. Compensation current: ieq = Id - gm*VSG - gds*VSD
   *
   * PMOS convention: gm sign is REVERSED vs NMOS. For NMOS, higher gate
   * voltage increases current. For PMOS, higher gate voltage decreases
   * current (turns off). The stamp uses addConductance(D, G, -gm) to
   * capture this inversion.
   *
   * Three device types:
   * - isDiodeLoad: depletion-mode (Vth < 0), always ON, smooth I-V curve
   * - isLoad: enhancement-mode load, weaker Kp, ON when gate is LOW
   * - logic: enhancement-mode, full Kp, ON when gate is LOW
   *
   * @note RT-safe.
   */
  template <typename MnaSystemT>
  void stampTransistorsLevel1(MnaSystemT& mna, const std::vector<double>& prevV) const {
    // Initialize per-transistor data on first call
    if (transistorKp_.empty()) {
      computeTransistorKp();
    }
    if (prevVsg_.size() != transistors_.size()) {
      prevVsg_.assign(transistors_.size(), 0.0);
      prevVsd_.assign(transistors_.size(), 0.0);
    }

    // Classify components on first call
    if (componentMode_ && componentTypes_.empty()) {
      auto classification = classifyComponents(*this);
      componentTypes_ = std::move(classification.types);
      if (norOutputNets_.empty()) buildNorOutputSet();
      // Initialize latch stored values from prevV (captures warmup state).
      // ACC bits are initialized from readAccumulator (digital readback)
      // because binary switch warmup may leave marginal analog voltages.
      if (latchValues_.empty()) {
        // ACC: use initialAcc_ override if set, otherwise digital readback
        std::uint8_t warmupAcc = (initialAcc_ >= 0)
            ? static_cast<std::uint8_t>(initialAcc_ & 0xF)
            : readAccumulator(prevV);
        // (latch init for ACC, carry, then remaining nets below)
        for (int b = 0; b < 4; ++b) {
          if (accNets_[b] != 0 && accNets_[b] != vdd_) {
            latchValues_[accNets_[b]] = (warmupAcc & (1 << b)) ? 0.0 : VDD_VOLTAGE;
          }
        }
        // Carry
        if (cyNet_ > 0 && cyNet_ != vdd_) {
          bool cy = readCarry(prevV);
          latchValues_[cyNet_] = cy ? 0.0 : VDD_VOLTAGE;
        }
        // All other dynamic storage nets: threshold from prevV
        for (std::size_t i = 0; i < transistors_.size(); ++i) {
          if (componentTypes_[i] == ComponentType::DYNAMIC_STORAGE) {
            auto& t = transistors_[i];
            auto initNet = [&](mna::NetID net) {
              if (net != 0 && net != vdd_ && latchValues_.count(net) == 0) {
                double v = (net < prevV.size()) ? prevV[net] : VDD_VOLTAGE;
                latchValues_[net] = (v > VDD_VOLTAGE * 0.5) ? VDD_VOLTAGE : 0.0;
              }
            };
            initNet(t.drain);
            initNet(t.source);
          }
        }
      }
    }

    for (std::size_t idx = 0; idx < transistors_.size(); ++idx) {
      const auto& t = transistors_[idx];

      // Skip transistors connected to clock nets (CLK1, CLK2).
      // Clocks are external inputs driven behaviorally. Level 1 stamps
      // on CLK-connected transistors add gm column entries that fight
      // the behavioral clock drive.
      if (componentMode_ && (t.gate == clk1Net_ || t.gate == clk2Net_)) {
        // Use binary switch for clock-gated transistors
        const auto& bp = bsParams_;
        double vs = std::max(prevV[t.drain], prevV[t.source]);
        double vgs = prevV[t.gate] - vs;
        double gds;
        if (vgs < -bp.vth - bp.subthMargin) {
          gds = bp.gOn;
        } else if (vgs > -bp.vth + bp.subthMargin) {
          gds = bp.gOff;
        } else {
          gds = bp.gSubth;
        }
        mna.addConductance(t.drain, t.source, gds);
        continue;
      }

      // Component-mode dispatch: each type gets its own stamp strategy.
      // NOR gates + pass gates: Level 1 (proven physics)
      // Dynamic storage: binary switch (proven for ACC=5)
      // Standalone loads: resistive G_LOAD
      if (componentMode_ && !componentTypes_.empty()) {
        auto ctype = componentTypes_[idx];
        if (ctype == ComponentType::STANDALONE_LOAD ||
            (ctype == ComponentType::NOR_GATE_MEMBER && t.isDiodeLoad)) {
          // Depletion loads: fixed conductance (always-ON pull-up)
          mna.addConductance(t.drain, t.source, bsParams_.gLoad);
          continue;
        }
        if (ctype == ComponentType::DYNAMIC_STORAGE) {
          // Sub-classify: if gate is a NOR output net, this transistor
          // is driven by proven physics and can use Level 1 stamps.
          if (norOutputNets_.count(t.gate)) {
            // NOR-output-gated: fall through to Level 1 stamp below.
          } else {
            // Latch feedback core: dispatched to a virtual hook so a
            // derived class (Intel4004GridLevel2) can plug in BSIM3
            // physics without modifying L1.
            stampLatchFeedbackTransistor(mna, idx, prevV);
            continue;
          }
        }
        if (ctype == ComponentType::PASS_GATE) {
          // Pass gates: Level 1 physics (fall through to stamp below).
          // Proven in isolated test: correct transfer when ON, charge
          // retention with cap companion when OFF, proper selectivity
          // with multiple pass gates on same storage node.
        }
        // NOR_GATE_MEMBER: falls through to Level 1 stamp below
      }

      // Resistive load mode (legacy, used when componentMode_ is false)
      if (resistiveLoads_ && t.isDiodeLoad) {
        mna.addConductance(t.drain, t.source, bsParams_.gLoad);
        continue;
      }

      // Use fixed drain/source from SPICE netlist (no voltage-dependent swap).
      //
      // SPICE convention: use netlist terminal
      // labels but set mode based on VDS sign. Normal mode (VDS>=0):
      // drain at higher potential. Reverse mode (VDS<0): drain at lower.
      // The stamp positions stay the same - only VALUES change via xnrm/xrev.
      const double VS = prevV[t.source];
      const double VD = prevV[t.drain];
      const double VG = prevV[t.gate];

      // Per-transistor parameters from calibrated W/L bins.
      // sameVtoMode_ = true: all transistors use VTH_ENH (proven for ACC=5).
      double kp = transistorKp_[idx];
      double vth = sameVtoMode_ ? VTH_ENH : (t.isDiodeLoad ? VTH_DEP : VTH_ENH);
      MosfetLevel1Params PARAMS{.Kp = kp, .Vth = vth, .lambda = LAMBDA};

      // PMOS voltages in NMOS-mirror convention (positive when ON)
      double VSG = VS - VG;
      double VSD = VS - VD;

      // Per-device voltage limiting (fetlim/limvds).
      if (idx < prevVsg_.size()) {
        VSG = MosfetLevel1::fetlim(VSG, prevVsg_[idx], PARAMS.Vth);
        VSD = MosfetLevel1::limvds(VSD, prevVsd_[idx]);
      }
      prevVsg_[idx] = VSG;
      prevVsd_[idx] = VSD;

      // SPICE mode selection: VDS determines normal vs reverse.
      // Stamp positions FIXED to netlist terminals. xnrm/xrev adjust values.
      int xnrm, xrev;
      double evalVgs, evalVds;
      if (VSD >= 0.0) {
        // Normal mode: source at higher potential than drain
        xnrm = 1;
        xrev = 0;
        evalVgs = VSG;
        evalVds = VSD;
      } else {
        // Reverse mode: drain at higher potential than source
        xnrm = 0;
        xrev = 1;
        evalVgs = VD - VG;
        evalVds = VD - VS;
        if (idx < prevVsg_.size()) {
          evalVgs = MosfetLevel1::fetlim(evalVgs, prevVsg_[idx], PARAMS.Vth);
          evalVds = MosfetLevel1::limvds(evalVds, prevVsd_[idx]);
        }
      }

      double vgsM = std::max(evalVgs, 0.0);
      double vdsM = std::max(evalVds, 0.0);
      const auto SV = MosfetLevel1::stampValues(vgsM, vdsM, PARAMS);
      const double id = SV.id;
      const double gm = SV.gm;
      const double gdsDevice = SV.gds;
      const double gdsStamp = std::max(gdsDevice, G_MIN);

      // Compensation current uses DEVICE gds, not G_MIN-augmented.
      // G_MIN is a numerical stabilizer; including it in cdreq cancels it.
      double cdreq;
      if (xnrm == 1) {
        cdreq = -(id - gdsDevice * VSD - gm * VSG);
      } else {
        cdreq = (id - gdsDevice * (-VSD) - gm * (VD - VG));
      }

      // MOSFET Jacobian stamp. Positions use netlist terminals.
      // DP=t.drain, SP=t.source. xnrm/xrev control gm distribution.
      mna.addConductance(t.drain, t.source, gdsStamp);         // Symmetric gds
      mna.addMatrixEntry(t.drain, t.drain, xrev * gm);         // DPdp += xrev*gm
      mna.addMatrixEntry(t.source, t.source, xnrm * gm);       // SPsp += xnrm*gm
      mna.addMatrixEntry(t.drain, t.gate, (xnrm - xrev) * gm); // DPg += (xnrm-xrev)*gm
      mna.addMatrixEntry(t.drain, t.source, -static_cast<double>(xnrm) * gm); // DPsp += -xnrm*gm
      mna.addMatrixEntry(t.source, t.gate, -(xnrm - xrev) * gm); // SPg += -(xnrm-xrev)*gm
      mna.addMatrixEntry(t.source, t.drain, -static_cast<double>(xrev) * gm); // SPdp += -xrev*gm

      // RHS: -cdreq at drain, +cdreq at source
      // Our addCurrent(a, b, val): I[a]+=val, I[b]-=val
      mna.addCurrent(t.drain, t.source, -cdreq);
    }

    // Stamp behavioral latches for dynamic storage nodes.
    // Must come AFTER transistor stamps so voltage sources override any
    // conductance stamps that touch managed nets.
    // Behavioral latch: force dynamic storage nets to clean rail voltages.
    // Uses voltage sources (like driveNet) to absolutely fix storage net
    // voltages. The stored values are updated by the forceBehavioral/
    // nrLimitCallback path in traceExecuteByte, which samples pass gate
    // states and updates latchValues_ between NR iterations.
    if (applyBehavioralLatchOverlay_ && componentMode_ && !latchValues_.empty()) {
      if (latchOverlayConductance_ > 0.0) {
        const double G = latchOverlayConductance_;
        for (auto& [net, storedV] : latchValues_) {
          mna.addConductance(net, circuit::Circuit::ground(), G);
          mna.addCurrent(net, circuit::Circuit::ground(), G * storedV);
        }
      } else {
        for (auto& [net, storedV] : latchValues_) {
          mna.addVoltageSource(net, 0, storedV);
        }
      }
    }
  }

  /* ----------------------------- Level 1 Parasitic Caps ----------------------------- */

  /**
   * @brief Stamp parasitic caps with Level 1 capacitance (CPARA_L1).
   *
   * Same backward Euler companion as base class but with Level 1 CPARA.
   *
   * @note RT-safe.
   */
  template <typename MnaSystemT>
  void stampParasiticCapsLevel1(MnaSystemT& mna, const std::vector<double>& prevV) const {
    if (stepDt_ <= 0.0) {
      return;
    }
    const double GEQ = CPARA_L1 / stepDt_;
    const std::size_t N = prevV.size();
    for (mna::NetID n = 1; n < N; ++n) {
      if (n == vdd_) {
        continue;
      }
      mna.addConductance(n, circuit::Circuit::ground(), GEQ);
      mna.addCurrent(n, circuit::Circuit::ground(), GEQ * prevV[n]);
    }
  }
  /* ----------------------------- GMIN Conductance ----------------------------- */

  /**
   * @brief Stamp GMIN from every internal node to ground.
   *
   * Standard SPICE convergence aid. Adds a tiny conductance (1e-12 S) from
   * each node to ground, ensuring the MNA matrix diagonal is never zero.
   * Critical for the asymmetric VCCS stamp which doesn't add gate-row entries.
   *
   * @note RT-safe.
   */
  template <typename MnaSystemT> void stampGmin(MnaSystemT& mna, std::size_t netCount) const {
    if (norOutputNets_.empty()) {
      buildNorOutputSet();
    }
    for (mna::NetID n = 1; n < netCount; ++n) {
      if (n == vdd_)
        continue;
      double gmin = gminTransient_;
      // Driven nets (NOR outputs, clocks) get a smaller GMIN if configured,
      // so strong gminTransient_ on floating nets doesn't distort logic levels.
      if (gminDriven_ > 0.0 &&
          (norOutputNets_.count(n) || n == clk1Net_ || n == clk2Net_)) {
        gmin = gminDriven_;
      }
      mna.addConductance(n, circuit::Circuit::ground(), gmin);
    }
  }

  /// Build set of nets that are NOR gate outputs (have depletion load).
  /// These nets are actively driven and don't need GMIN anchoring.
  void buildNorOutputSet() const {
    norOutputNets_.clear();
    for (const auto& t : transistors_) {
      if (t.isDiodeLoad) {
        // Depletion load output is the non-VDD terminal
        mna::NetID outNet = (t.drain == vdd_) ? t.source : t.drain;
        if (outNet != 0)
          norOutputNets_.insert(outNet);
      }
    }
  }

  mutable std::unordered_set<mna::NetID> norOutputNets_;
};

} // namespace sim::electronics::intel4004

#endif // APEX_SIM_ELECTRONICS_CPU_INTEL4004_INTEL4004GRIDLEVEL1_HPP
