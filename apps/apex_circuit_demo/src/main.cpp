/**
 * @file main.cpp
 * @brief General-purpose electronics circuit demo (gates + analog filter).
 *
 * Shows how to compose the apex electronics simulation libraries to build
 * and analyse arbitrary circuits without any Intel 4004-specific tooling:
 *
 *   --circuit gates       7 CMOS logic gates at the transistor level
 *                         (NOT, NAND, NOR, AND, OR, XOR, XNOR), each
 *                         built from real MOSFETs and solved via MNA
 *                         with MosfetLevel1 (Shichman-Hodges) physics.
 *                         Output voltages -- not boolean 0/1.
 *
 *   --circuit rc-lowpass  First-order RC low-pass filter, transient
 *                         step response via Backward Euler, compared
 *                         against the closed-form analytical solution
 *                         V_out(t) = V_in * (1 - exp(-t/tau)).
 *
 * Each mode is a self-contained example of how to use the apex Circuit
 * API + device models + analysis to build a simulation; the same
 * pattern applies to user-defined circuits.
 *
 * Usage:
 *   ./ApexCircuitDemo                              # default: gates
 *   ./ApexCircuitDemo --circuit gates              # all 7 gates
 *   ./ApexCircuitDemo --circuit rc-lowpass         # default 1k, 1uF
 *   ./ApexCircuitDemo --circuit rc-lowpass --r 10e3 --c 1e-6
 *   ./ApexCircuitDemo --circuit rc-lowpass --vstep 3.3 --duration 5e-3
 */

#include "src/sim/electronics/amplifiers/inc/BjtCommonEmitter.hpp"
#include "src/sim/electronics/filters/inc/RcLowPass.hpp"
#include "src/sim/electronics/gates/inc/CmosGateCircuits.hpp"
#include "src/sim/electronics/algorithms/transient/inc/TransientConfig.hpp"

#include <fmt/format.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using sim::electronics::amplifiers::BjtCommonEmitter;
using sim::electronics::devices::nonlinear::MosfetLevel1Params;
using sim::electronics::filters::RcLowPass;
using sim::electronics::gates::CmosAndCircuit;
using sim::electronics::gates::CmosInverterCircuit;
using sim::electronics::gates::CmosNandCircuit;
using sim::electronics::gates::CmosNorCircuit;
using sim::electronics::gates::CmosOrCircuit;
using sim::electronics::gates::CmosXnorCircuit;
using sim::electronics::gates::CmosXorCircuit;
using sim::electronics::transient::IntegrationMethod;
using sim::electronics::transient::TransientState;

/* ----------------------------- Constants ----------------------------- */

/// Default supply voltage for the gate library (V).
static constexpr double GATE_VDD = 5.0;

/// Default MOSFET width (m) for the gate library.
static constexpr double GATE_W = 10e-6;

/// Default MOSFET length (m) for the gate library.
static constexpr double GATE_L = 1e-6;

/// Default RC filter resistance (ohm).
static constexpr double DEFAULT_RC_R = 1e3;

/// Default RC filter capacitance (F).
static constexpr double DEFAULT_RC_C = 1e-6;

/// Default RC step input voltage (V).
static constexpr double DEFAULT_RC_VSTEP = 5.0;

/// Default RC simulation duration (s).
static constexpr double DEFAULT_RC_DURATION = 5e-3;

/// Default RC report rows.
static constexpr int DEFAULT_RC_STEPS = 100;

/// Sub-steps used for transient integration (independent of report rows).
static constexpr int RC_SUB_STEPS = 1000;

/// Default common-emitter supply voltage (V).
static constexpr double DEFAULT_CE_VCC = 12.0;

/// Default common-emitter collector resistor (ohm).
static constexpr double DEFAULT_CE_RC = 1e3;

/// Default common-emitter base resistor (ohm).
static constexpr double DEFAULT_CE_RB = 100e3;

/* ----------------------------- CliArgs ----------------------------- */

enum class CircuitKind {
  GATES,
  RC_LOWPASS,
  COMMON_EMITTER,
};

struct CliArgs {
  CircuitKind circuit = CircuitKind::GATES;

  // RC filter parameters.
  double resistance = DEFAULT_RC_R;
  double capacitance = DEFAULT_RC_C;
  double vStep = DEFAULT_RC_VSTEP;
  double duration = DEFAULT_RC_DURATION;
  int steps = DEFAULT_RC_STEPS;

  // BJT common-emitter parameters.
  double vcc = DEFAULT_CE_VCC;
  double rc = DEFAULT_CE_RC;
  double rb = DEFAULT_CE_RB;
};

/* ----------------------------- CLI ----------------------------- */

static void printUsage() {
  fmt::print(
      "Apex Circuit Demo\n\n"
      "Usage: ApexCircuitDemo [options]\n"
      "  --circuit KIND       Circuit to simulate. One of:\n"
      "                         gates           (default) -- 7 CMOS gates at transistor level\n"
      "                         rc-lowpass                -- RC low-pass filter (transient)\n"
      "                         common-emitter            -- BJT common-emitter amp (DC bias)\n"
      "  --r OHMS             RC filter resistance (default: 1e3)\n"
      "  --c FARADS           RC filter capacitance (default: 1e-6)\n"
      "  --vstep VOLTS        RC step input voltage (default: 5.0)\n"
      "  --duration SEC       RC simulation duration (default: 5e-3)\n"
      "  --steps N            RC report rows (default: 100)\n"
      "  --vcc VOLTS          Common-emitter supply voltage (default: 12)\n"
      "  --rc-collector OHMS  Common-emitter collector resistor (default: 1k)\n"
      "  --rb-base OHMS       Common-emitter base resistor (default: 100k)\n"
      "  -h, --help           Show this help\n\n"
      "Examples:\n"
      "  ApexCircuitDemo                                    # gates default\n"
      "  ApexCircuitDemo --circuit rc-lowpass               # 1k, 1uF, 5V step\n"
      "  ApexCircuitDemo --circuit rc-lowpass --r 10e3 --c 1e-6\n"
      "  ApexCircuitDemo --circuit common-emitter           # 12V VCC, 1k Rc, 100k Rb\n"
      "  ApexCircuitDemo --circuit common-emitter --vcc 9 --rb-base 47e3\n");
}

static CliArgs parseArgs(int argc, char* argv[]) {
  CliArgs args;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--circuit") == 0 && i + 1 < argc) {
      const std::string kind = argv[++i];
      if (kind == "gates") {
        args.circuit = CircuitKind::GATES;
      } else if (kind == "rc-lowpass" || kind == "filter") {
        args.circuit = CircuitKind::RC_LOWPASS;
      } else if (kind == "common-emitter" || kind == "bjt-ce") {
        args.circuit = CircuitKind::COMMON_EMITTER;
      } else {
        fmt::print(stderr,
                   "ERROR: unknown --circuit '{}' "
                   "(expected: gates, rc-lowpass, common-emitter)\n",
                   kind);
        std::exit(1);
      }
    } else if (std::strcmp(argv[i], "--r") == 0 && i + 1 < argc) {
      args.resistance = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i], "--c") == 0 && i + 1 < argc) {
      args.capacitance = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i], "--vstep") == 0 && i + 1 < argc) {
      args.vStep = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
      args.duration = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i], "--steps") == 0 && i + 1 < argc) {
      args.steps = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--vcc") == 0 && i + 1 < argc) {
      args.vcc = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i], "--rc-collector") == 0 && i + 1 < argc) {
      args.rc = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i], "--rb-base") == 0 && i + 1 < argc) {
      args.rb = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
      printUsage();
      std::exit(0);
    }
  }
  return args;
}

/* ----------------------------- File Helpers ----------------------------- */

/// Print a 1-input gate's voltage transfer characteristic.
static void printOneInputGate(const char* name, int transistors,
                              CmosInverterCircuit& gate, double vdd) {
  fmt::print("\n  {} ({} MOSFETs):\n", name, transistors);
  fmt::print("    Vin (V)  | Vout (V)\n");
  fmt::print("    ---------+---------\n");

  const double INPUTS[] = {0.0, vdd / 4.0, vdd / 2.0, 3.0 * vdd / 4.0, vdd};
  for (double vin : INPUTS) {
    gate.setInput(vin);
    const double VOUT = gate.computeDC();
    fmt::print("    {:>7.2f}  | {:>7.4f}\n", vin, VOUT);
  }
}

/// Print a 2-input gate's voltage truth table.
static void printTwoInputGate(const char* name, int transistors, auto& gate, double vdd) {
  fmt::print("\n  {} ({} MOSFETs):\n", name, transistors);
  fmt::print("    VinA (V) | VinB (V) | Vout (V)\n");
  fmt::print("    ---------+----------+---------\n");

  const double PAIRS[][2] = {{0.0, 0.0}, {0.0, vdd}, {vdd, 0.0}, {vdd, vdd}};
  for (auto& p : PAIRS) {
    gate.setInputs(p[0], p[1]);
    const double VOUT = gate.computeDC();
    fmt::print("    {:>7.2f}  | {:>8.2f} | {:>7.4f}\n", p[0], p[1], VOUT);
  }
}

/* ----------------------------- API ----------------------------- */

static int runGates() {
  const MosfetLevel1Params NMOS_PARAMS{.Kp = 120e-6, .Vth = 0.7, .lambda = 0.02};
  const MosfetLevel1Params PMOS_PARAMS{.Kp = 60e-6, .Vth = 0.7, .lambda = 0.02};

  fmt::print("=======================================================\n");
  fmt::print("  CMOS Digital Logic Gates\n");
  fmt::print("  Transistor-Level Circuit Simulation\n");
  fmt::print("=======================================================\n");
  fmt::print("  VDD = {:.1f}V, NMOS Kp = {:.0f} uA/V^2, PMOS Kp = {:.0f} uA/V^2\n",
             GATE_VDD, NMOS_PARAMS.Kp * 1e6, PMOS_PARAMS.Kp * 1e6);
  fmt::print("  Vth = {:.1f}V, W = {:.0f} um, L = {:.0f} um\n",
             NMOS_PARAMS.Vth, GATE_W * 1e6, GATE_L * 1e6);

  CmosInverterCircuit inv(GATE_VDD, GATE_W, GATE_L, NMOS_PARAMS, PMOS_PARAMS);
  inv.build();
  printOneInputGate("CMOS NOT / Inverter", 2, inv, GATE_VDD);

  CmosNandCircuit nandGate(GATE_VDD, GATE_W, GATE_L, NMOS_PARAMS, PMOS_PARAMS);
  nandGate.build();
  printTwoInputGate("CMOS NAND", 4, nandGate, GATE_VDD);

  CmosNorCircuit norGate(GATE_VDD, GATE_W, GATE_L, NMOS_PARAMS, PMOS_PARAMS);
  norGate.build();
  printTwoInputGate("CMOS NOR", 4, norGate, GATE_VDD);

  CmosAndCircuit andGate(GATE_VDD, GATE_W, GATE_L, NMOS_PARAMS, PMOS_PARAMS);
  andGate.build();
  printTwoInputGate("CMOS AND (NAND + NOT)", 6, andGate, GATE_VDD);

  CmosOrCircuit orGate(GATE_VDD, GATE_W, GATE_L, NMOS_PARAMS, PMOS_PARAMS);
  orGate.build();
  printTwoInputGate("CMOS OR (NOR + NOT)", 6, orGate, GATE_VDD);

  CmosXorCircuit xorGate(GATE_VDD, GATE_W, GATE_L, NMOS_PARAMS, PMOS_PARAMS);
  xorGate.build();
  printTwoInputGate("CMOS XOR (4 NAND gates)", 16, xorGate, GATE_VDD);

  CmosXnorCircuit xnorGate(GATE_VDD, GATE_W, GATE_L, NMOS_PARAMS, PMOS_PARAMS);
  xnorGate.build();
  printTwoInputGate("CMOS XNOR (XOR + NOT)", 18, xnorGate, GATE_VDD);

  fmt::print("\n=======================================================\n");
  fmt::print("  All gates simulated at transistor level.\n");
  fmt::print("  Output voltages reflect MosfetLevel1 physics:\n");
  fmt::print("  HIGH ~ {:.1f}V, LOW ~ 0V (exact values depend on model)\n", GATE_VDD);
  fmt::print("=======================================================\n");
  return 0;
}

static int runRcLowPass(const CliArgs& args) {
  RcLowPass filter(args.resistance, args.capacitance);
  filter.build();

  fmt::print("=======================================================\n");
  fmt::print("  RC Low-Pass Filter\n");
  fmt::print("  Transient step response vs analytical solution\n");
  fmt::print("=======================================================\n");
  fmt::print("  R          = {:.4g} ohm\n", args.resistance);
  fmt::print("  C          = {:.4g} F\n", args.capacitance);
  fmt::print("  tau = R*C  = {:.4g} s\n", filter.tau());
  fmt::print("  f_c        = {:.4g} Hz\n", filter.cutoffHz());
  fmt::print("  V_step     = {:.4g} V\n", args.vStep);
  fmt::print("  duration   = {:.4g} s ({:.2f} tau)\n", args.duration, args.duration / filter.tau());
  fmt::print("\n");

  // DC initial condition at 0V before the step.
  TransientState state;
  state.resize(filter.circuit().netCount(), 0);
  filter.setInputVoltage(0.0);
  filter.circuit().computeDC(state);

  // Apply step input at t=0 and integrate via Backward Euler.
  filter.setInputVoltage(args.vStep);
  filter.circuit().solver().setIntegrationMethod(IntegrationMethod::BACKWARD_EULER);

  const double DT = args.duration / RC_SUB_STEPS;
  const int STEPS_PER_REPORT = RC_SUB_STEPS / args.steps;

  fmt::print("--- Step Response ---\n");
  fmt::print("  {:>10}  {:>12}  {:>12}  {:>12}  {:>10}\n", "t (s)", "t/tau", "V_sim (V)",
             "V_analytic", "error %");
  fmt::print("  {:->10}  {:->12}  {:->12}  {:->12}  {:->10}\n", "", "", "", "", "");

  // Initial row at t=0 before any sub-steps run.
  {
    const double V = state.nodeVoltages[filter.outNet()];
    const double A = filter.analyticalStepResponse(args.vStep, 0.0);
    fmt::print("  {:10.4g}  {:12.4f}  {:12.6f}  {:12.6f}  {:10.4f}\n", 0.0, 0.0, V, A, 0.0);
  }

  double t = 0.0;
  for (int row = 1; row <= args.steps; ++row) {
    for (int s = 0; s < STEPS_PER_REPORT; ++s) {
      filter.circuit().solver().step(DT, state);
      t += DT;
    }
    const double V = state.nodeVoltages[filter.outNet()];
    const double A = filter.analyticalStepResponse(args.vStep, t);
    const double ERR = (A != 0.0) ? std::abs(V - A) / A * 100.0 : 0.0;
    fmt::print("  {:10.4g}  {:12.4f}  {:12.6f}  {:12.6f}  {:10.4f}\n", t, t / filter.tau(), V, A,
               ERR);
  }

  fmt::print("\n--- Magnitude Response ---\n");
  fmt::print("  {:>10}  {:>12}  {:>10}\n", "f (Hz)", "|H(jomega)|", "dB");
  fmt::print("  {:->10}  {:->12}  {:->10}\n", "", "", "");
  const double FREQS[] = {0.0,
                          filter.cutoffHz() / 10.0,
                          filter.cutoffHz() / 2.0,
                          filter.cutoffHz(),
                          2.0 * filter.cutoffHz(),
                          10.0 * filter.cutoffHz()};
  for (double f : FREQS) {
    const double MAG = filter.analyticalMagnitudeResponse(f);
    const double DB = (MAG > 0.0) ? 20.0 * std::log10(MAG) : -1e9;
    fmt::print("  {:10.4g}  {:12.6f}  {:10.4f}\n", f, MAG, DB);
  }

  fmt::print("\n=======================================================\n");
  fmt::print("  Filter simulation complete.\n");
  fmt::print("=======================================================\n");
  return 0;
}

static int runCommonEmitter(const CliArgs& args) {
  BjtCommonEmitter amp(args.vcc, args.rc, args.rb);

  fmt::print("=======================================================\n");
  fmt::print("  BJT Common-Emitter Amplifier\n");
  fmt::print("  DC operating-point analysis\n");
  fmt::print("=======================================================\n");
  fmt::print("  VCC = {:.4g} V\n", args.vcc);
  fmt::print("  RC  = {:.4g} ohm  (collector resistor)\n", args.rc);
  fmt::print("  RB  = {:.4g} ohm  (base resistor)\n", args.rb);
  fmt::print("  BJT = NPN, default Ebers-Moll params (~2N2222 / 2N3904)\n");
  fmt::print("\n");

  if (!amp.computeDC()) {
    fmt::print(stderr, "ERROR: Newton-Raphson did not converge for VCC={}, RC={}, RB={}\n",
               args.vcc, args.rc, args.rb);
    return 1;
  }

  const double VC = amp.collectorVoltage();
  const double VB = amp.baseVoltage();
  const double IC = amp.collectorCurrent();
  const double VCE = VC; // emitter at GND
  const double VRC = args.vcc - VC;

  fmt::print("--- DC Operating Point ---\n");
  fmt::print("  V_B   = {:8.4f} V    (base)\n", VB);
  fmt::print("  V_C   = {:8.4f} V    (collector)\n", VC);
  fmt::print("  V_CE  = {:8.4f} V    (collector-emitter, target ~ VCC/2 for max swing)\n", VCE);
  fmt::print("  V_RC  = {:8.4f} V    (drop across RC)\n", VRC);
  fmt::print("  I_C   = {:8.4f} mA   (collector current)\n", IC * 1e3);
  fmt::print("  I_B   = {:8.4f} uA   (base current = (VCC - V_B) / RB)\n",
             (args.vcc - VB) / args.rb * 1e6);

  // Region check: NPN BJT in active region requires V_BE >= ~0.6V and V_CE > V_BE.
  const double VBE = VB; // emitter at GND
  const char* REGION = (VBE < 0.55)              ? "cutoff (V_BE too low)"
                       : (VCE < VBE)             ? "saturation (V_CE < V_BE)"
                       : (VCE > 0.1 * args.vcc)  ? "active (linear amplification)"
                                                : "near-saturation";
  fmt::print("\n  Operating region: {}\n", REGION);

  fmt::print("\n=======================================================\n");
  fmt::print("  Common-emitter simulation complete.\n");
  fmt::print("=======================================================\n");
  return 0;
}

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const auto ARGS = parseArgs(argc, argv);
  switch (ARGS.circuit) {
  case CircuitKind::GATES:
    return runGates();
  case CircuitKind::RC_LOWPASS:
    return runRcLowPass(ARGS);
  case CircuitKind::COMMON_EMITTER:
    return runCommonEmitter(ARGS);
  }
  return 0;
}
