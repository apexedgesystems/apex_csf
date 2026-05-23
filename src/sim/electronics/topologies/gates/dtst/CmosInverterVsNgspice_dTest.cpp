/**
 * @file CmosInverterVsNgspice_dTest.cpp
 * @brief Cross-validation of CMOS inverter circuit model against ngspice.
 *
 * Runs the same inverter circuit in our gate model (MosfetLevel1 via MNA)
 * and in ngspice (Level 1 MOSFET via libngspice), comparing output voltages.
 *
 * Parameters matched between the two solvers:
 *   NMOS: KP=12u (process), W=10u, L=1u -> effective Kp=120u, VTO=0.7, LAMBDA=0.02
 *   PMOS: KP=6u  (process), W=10u, L=1u -> effective Kp=60u,  VTO=-0.7, LAMBDA=0.02
 *   VDD = 5V
 *
 * If both solvers use the same Shichman-Hodges equations, the output
 * voltages should match within a tight tolerance.
 */

#include "src/sim/electronics/algorithms/spice/ngspice/inc/NgspiceWrapper.hpp"
#include "src/sim/electronics/topologies/gates/inc/CmosGateCircuits.hpp"

#include <fmt/format.h>
#include <gtest/gtest.h>

#include <cmath>
#include <string>

using sim::electronics::algorithms::spice::ngspice::NgspiceStatus;
using sim::electronics::algorithms::spice::ngspice::NgspiceWrapper;
using sim::electronics::devices::nonlinear::MosfetLevel1Params;
using sim::electronics::topologies::gates::CmosInverterCircuit;

/* ----------------------------- Constants ----------------------------- */

static constexpr double VDD = 5.0;
static constexpr double W = 10e-6;
static constexpr double L = 1e-6;

static const MosfetLevel1Params NMOS_PARAMS{.Kp = 120e-6, .Vth = 0.7, .lambda = 0.02};
static const MosfetLevel1Params PMOS_PARAMS{.Kp = 60e-6, .Vth = 0.7, .lambda = 0.02};

/* ----------------------------- Helpers ----------------------------- */

/**
 * @brief Build ngspice netlist for a CMOS inverter at a given input voltage.
 *
 * PMOS: source=VDD, gate=in, drain=out. NMOS: drain=out, gate=in, source=GND.
 * ngspice Level 1 with KP as process parameter (our Kp = KP * W / L).
 */
static std::string buildInverterNetlist(double vin) {
  // ngspice KP = our_Kp * L / W
  const double NMOS_KP = NMOS_PARAMS.Kp * L / W;
  const double PMOS_KP = PMOS_PARAMS.Kp * L / W;

  return fmt::format("CMOS Inverter Verification\n"
                     "VDD vdd 0 {:.1f}\n"
                     "VIN in 0 {:.6f}\n"
                     "M1 out in vdd vdd PMOD W={:.0e} L={:.0e}\n"
                     "M2 out in 0 0 NMOD W={:.0e} L={:.0e}\n"
                     ".model PMOD PMOS (LEVEL=1 VTO={:.4f} KP={:.6e} LAMBDA={:.4f})\n"
                     ".model NMOD NMOS (LEVEL=1 VTO={:.4f} KP={:.6e} LAMBDA={:.4f})\n"
                     ".op\n"
                     ".end\n",
                     VDD, vin, W, L, W, L, -PMOS_PARAMS.Vth, PMOS_KP, PMOS_PARAMS.lambda,
                     NMOS_PARAMS.Vth, NMOS_KP, NMOS_PARAMS.lambda);
}

/* ----------------------------- Tests ----------------------------- */

/** @test CMOS inverter: our circuit model vs ngspice at input = 0V. */
TEST(CmosInverterVsNgspice, InputLow) {
  const double VIN = 0.0;

  // Our circuit model
  CmosInverterCircuit inv(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
  inv.build();
  inv.setInput(VIN);
  double OUR_VOUT = inv.computeDC();

  // ngspice
  NgspiceWrapper ngspice;
  std::string NETLIST = buildInverterNetlist(VIN);
  NgspiceStatus status = ngspice.loadNetlistFromString(NETLIST);

  if (status == NgspiceStatus::ERROR_LIBNGSPICE_NOT_AVAILABLE) {
    GTEST_SKIP() << "libngspice not available";
  }
  ASSERT_EQ(status, NgspiceStatus::OK) << "Failed to load netlist";

  status = ngspice.runDcOperatingPoint();
  ASSERT_EQ(status, NgspiceStatus::OK) << "DC op failed";

  double NGSPICE_VOUT = 0.0;
  status = ngspice.getNodeVoltage("out", NGSPICE_VOUT);
  ASSERT_EQ(status, NgspiceStatus::OK) << "Failed to read output voltage";

  fmt::print("  Input = {:.1f}V: ours = {:.6f}V, ngspice = {:.6f}V, diff = {:.6f}V\n", VIN,
             OUR_VOUT, NGSPICE_VOUT, std::abs(OUR_VOUT - NGSPICE_VOUT));

  EXPECT_NEAR(OUR_VOUT, NGSPICE_VOUT, 0.1) << "Inverter output should match ngspice within 100mV";
}

/** @test CMOS inverter: our circuit model vs ngspice at input = VDD. */
TEST(CmosInverterVsNgspice, InputHigh) {
  const double VIN = VDD;

  CmosInverterCircuit inv(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
  inv.build();
  inv.setInput(VIN);
  double OUR_VOUT = inv.computeDC();

  NgspiceWrapper ngspice;
  std::string NETLIST = buildInverterNetlist(VIN);
  NgspiceStatus status = ngspice.loadNetlistFromString(NETLIST);

  if (status == NgspiceStatus::ERROR_LIBNGSPICE_NOT_AVAILABLE) {
    GTEST_SKIP() << "libngspice not available";
  }
  ASSERT_EQ(status, NgspiceStatus::OK);

  status = ngspice.runDcOperatingPoint();
  ASSERT_EQ(status, NgspiceStatus::OK);

  double NGSPICE_VOUT = 0.0;
  status = ngspice.getNodeVoltage("out", NGSPICE_VOUT);
  ASSERT_EQ(status, NgspiceStatus::OK);

  fmt::print("  Input = {:.1f}V: ours = {:.6f}V, ngspice = {:.6f}V, diff = {:.6f}V\n", VIN,
             OUR_VOUT, NGSPICE_VOUT, std::abs(OUR_VOUT - NGSPICE_VOUT));

  EXPECT_NEAR(OUR_VOUT, NGSPICE_VOUT, 0.1) << "Inverter output should match ngspice within 100mV";
}

/** @test CMOS inverter: transfer curve sweep vs ngspice. */
TEST(CmosInverterVsNgspice, TransferCurve) {
  constexpr int NUM_POINTS = 11;
  double maxDiff = 0.0;

  fmt::print("  Transfer curve comparison:\n");
  fmt::print("    Vin (V) | Ours (V)  | ngspice (V) | Diff (mV)\n");
  fmt::print("    --------+-----------+-------------+----------\n");

  for (int i = 0; i < NUM_POINTS; ++i) {
    const double VIN = VDD * static_cast<double>(i) / (NUM_POINTS - 1);

    CmosInverterCircuit inv(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
    inv.build();
    inv.setInput(VIN);
    double OUR_VOUT = inv.computeDC();

    NgspiceWrapper ngspice;
    std::string NETLIST = buildInverterNetlist(VIN);
    NgspiceStatus status = ngspice.loadNetlistFromString(NETLIST);

    if (status == NgspiceStatus::ERROR_LIBNGSPICE_NOT_AVAILABLE) {
      GTEST_SKIP() << "libngspice not available";
    }
    ASSERT_EQ(status, NgspiceStatus::OK);

    status = ngspice.runDcOperatingPoint();
    ASSERT_EQ(status, NgspiceStatus::OK);

    double NGSPICE_VOUT = 0.0;
    status = ngspice.getNodeVoltage("out", NGSPICE_VOUT);
    ASSERT_EQ(status, NgspiceStatus::OK);

    double DIFF = std::abs(OUR_VOUT - NGSPICE_VOUT);
    maxDiff = std::max(maxDiff, DIFF);

    fmt::print("    {:>7.2f} | {:>9.4f} | {:>11.4f} | {:>8.1f}\n", VIN, OUR_VOUT, NGSPICE_VOUT,
               DIFF * 1000.0);
  }

  fmt::print("    Max difference: {:.1f} mV\n", maxDiff * 1000.0);

  EXPECT_LT(maxDiff, 0.5) << "Max voltage difference should be under 500mV across transfer curve";
}
