/**
 * @file Level1Params_uTest.cpp
 * @brief Analytical parameter exploration for Level 1 PMOS inverter.
 *
 * Uses the closed-form solution for depletion-load PMOS inverter output voltage
 * to find parameters that produce clean logic levels. No MNA solve needed.
 *
 * Key equation (both transistors in saturation):
 *   Vout_low = Vth_enh + 2 * sqrt(Kp_dep / Kp_enh) * |Vth_dep|
 *   Vout_high = VDD (when logic PMOS is OFF, depletion pulls to rail)
 *
 * For clean digital logic: Vout_low should be < VDD * 0.1 (below 10% of rail).
 */

#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <iostream>

using sim::electronics::devices::nonlinear::MosfetLevel1;
using sim::electronics::devices::nonlinear::MosfetLevel1Params;

/* ----------------------------- Analytical VOL ----------------------------- */

/**
 * @brief Compute output LOW voltage for depletion-load PMOS inverter.
 *
 * When input=LOW (0V), both transistors are in saturation:
 * - Enhancement: Id = 0.5 * Kp_e * (Vout - Vth_e)^2
 * - Depletion:   Id = 0.5 * Kp_d * Vth_d^2
 *
 * Equating: Vout = Vth_e + |Vth_d| * sqrt(Kp_d / Kp_e)
 */
static double analyticalVol(double kpEnh, double vthEnh, double kpDep, double vthDep) {
  return vthEnh + std::fabs(vthDep) * std::sqrt(kpDep / kpEnh);
}

/* ----------------------------- Parameter Space ----------------------------- */

/** @test Show VOL for current parameters - explains why we get 1.5V. */
TEST(Level1Params, CurrentParametersVol) {
  double vol = analyticalVol(5e-3, 1.0, 3.125e-4, -2.0);
  std::cout << "  Current params: VOL = " << vol << "V (Vth_e=1.0, Kp_e=5e-3, "
            << "Kp_d=3.125e-4, Vth_d=-2.0)\n";
  EXPECT_NEAR(vol, 1.5, 0.1) << "Should match our NR and ngspice result";
}

/** @test Find Vth_enh that gives VOL < 0.5V with current Kp ratio. */
TEST(Level1Params, VthSweepForCleanLogic) {
  double kpEnh = 5e-3;
  double kpDep = 3.125e-4;
  double vthDep = -2.0;

  std::cout << "  Vth_enh sweep (Kp_enh=5e-3, Kp_d=3.125e-4, Vth_d=-2.0):\n";
  for (double vthE = 0.1; vthE <= 1.0; vthE += 0.1) {
    double vol = analyticalVol(kpEnh, vthE, kpDep, vthDep);
    std::cout << "    Vth_e=" << vthE << "V  =>  VOL=" << vol << "V\n";
  }

  // Find the threshold that gives VOL = 0.5V
  // 0.5 = Vth_e + 2 * sqrt(3.125e-4 / 5e-3) = Vth_e + 0.5
  // Vth_e = 0.0  ... but that's unphysical
  double volAt03 = analyticalVol(kpEnh, 0.3, kpDep, vthDep);
  std::cout << "  => Vth_e=0.3V gives VOL=" << volAt03 << "V\n";
  EXPECT_LT(volAt03, 1.0) << "Lower Vth should give lower VOL";
}

/** @test Increase Kp ratio (make depletion weaker). */
TEST(Level1Params, KpRatioSweep) {
  double vthEnh = 1.0;
  double vthDep = -2.0;
  double kpEnh = 5e-3;

  std::cout << "  Kp ratio sweep (Vth_e=1.0, Vth_d=-2.0, Kp_e=5e-3):\n";
  for (double ratio : {1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 500.0, 1000.0}) {
    double kpDep = kpEnh / ratio;
    double vol = analyticalVol(kpEnh, vthEnh, kpDep, vthDep);
    std::cout << "    Kp_e/Kp_d=" << ratio << "  =>  VOL=" << vol << "V\n";
  }
}

/** @test Real 4004 parameters: VDD=-15V, Vth_e=-2V, Vth_d=-4V (PMOS process). */
TEST(Level1Params, Real4004Parameters) {
  // Real Intel 4004 used -15V supply, PMOS-only.
  // Typical 10um PMOS (circa 1971):
  //   Enhancement: Vth ~ -1.5 to -2.5V, Kp ~ 10-20 uA/V^2
  //   Depletion:   Vth ~ -3 to -5V, Kp ~ 5-10 uA/V^2
  //
  // But we model in NMOS-mirror convention (positive VSG/VSD).
  // With VDD=15V (positive convention):
  //   Enhancement: Vth = 2.0V (ON when VSG > 2V)
  //   Depletion:   Vth = -4.0V (ON at VSG=0, overdrive=4V)

  double kpEnh = 20e-6; // 20 uA/V^2 (process parameter, not effective)
  double kpDep = 8e-6;  // 8 uA/V^2
  double vthEnh = 2.0;
  double vthDep = -4.0;

  double vol = analyticalVol(kpEnh, vthEnh, kpDep, vthDep);
  double volRatio = vol / 15.0; // Fraction of VDD

  std::cout << "  Real 4004 estimate (VDD=15V):\n";
  std::cout << "    Enhancement: Kp=20uA/V^2, Vth=2.0V\n";
  std::cout << "    Depletion:   Kp=8uA/V^2, Vth=-4.0V\n";
  std::cout << "    VOL = " << vol << "V (" << (volRatio * 100.0) << "% of VDD)\n";

  // With these params, VOL should be well below VDD/2
  // 30% VOL/VDD is typical for depletion-load PMOS logic.
  // With VDD=15V: VOL~4.5V, threshold~7.5V => 3V noise margin. Workable.
  EXPECT_LT(volRatio, 0.35) << "Real 4004 params should give VOL below 35% VDD";

  // Scale to VDD=5V preserving VOL/VDD ratio
  // Scale Vth proportionally: Vth_e = 2.0 * 5/15 = 0.67V
  // Scale Vth_d proportionally: Vth_d = -4.0 * 5/15 = -1.33V
  // Kp stays the same (process parameter).
  double vthEnh5V = vthEnh * (5.0 / 15.0);
  double vthDep5V = vthDep * (5.0 / 15.0);
  double vol5V = analyticalVol(kpEnh, vthEnh5V, kpDep, vthDep5V);
  double volRatio5V = vol5V / 5.0;

  std::cout << "  Scaled to VDD=5V:\n";
  std::cout << "    Enhancement: Kp=20uA/V^2, Vth=" << vthEnh5V << "V\n";
  std::cout << "    Depletion:   Kp=8uA/V^2, Vth=" << vthDep5V << "V\n";
  std::cout << "    VOL = " << vol5V << "V (" << (volRatio5V * 100.0) << "% of VDD)\n";

  EXPECT_LT(volRatio5V, 0.35) << "Scaled params should maintain same VOL/VDD ratio";
}
