#include "src/sim/electronics/algorithms/spice/ngspice/inc/NgspiceWrapper.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1.hpp"

#include <fmt/core.h>
#include <gtest/gtest.h>

#include <cmath>

using sim::electronics::devices::nonlinear::MosfetLevel1;
using sim::electronics::devices::nonlinear::MosfetLevel1Params;

/* ----------------------- NMOS I-V Verification -------------------------- */

/**
 * @test
 * Verify MosfetLevel1 NMOS against analytical Shichman-Hodges equations.
 *
 * Test circuit (SPICE netlist: fixtures/mosfet_level1_verification.sp):
 *   M1 drain gate 0 0 NMOS W=10u L=1u
 *   VDS = 5.0V, VGS = 2.0V
 *
 * Model parameters:
 *   VTO (Vth) = 0.7V
 *   KP = 100u A/V^2
 *   LAMBDA = 0.02 1/V
 *
 * This test verifies our implementation matches the analytical equations.
 * After generating ngspice reference data, we can add comparison against
 * ngspice output.
 */
TEST(MosfetLevel1, VerifyNmosAgainstAnalytical) {
  // 1. MOSFET parameters (from .sp file)
  MosfetLevel1Params params;
  params.Kp = 100e-6;   // Transconductance parameter (A/V^2)
  params.Vth = 0.7;     // Threshold voltage (V)
  params.lambda = 0.02; // Channel-length modulation (1/V)

  // 2. Operating point voltages
  double vgs = 2.0; // Gate-source voltage (V)
  double vds = 5.0; // Drain-source voltage (V)

  // 3. Calculate drain current with our model
  double ids = MosfetLevel1::current(vgs, vds, params);

  // 4. Analytical calculation using Shichman-Hodges equations
  //
  // Check region:
  //   VGS - Vth = 2.0 - 0.7 = 1.3V
  //   VDS = 5.0V
  //   Since VDS (5.0V) >= VGS-Vth (1.3V), transistor is in SATURATION
  //
  // Saturation equation:
  //   IDS = 0.5 * Kp * (VGS - Vth)^2 * (1 + lambda * VDS)
  //
  double vgst = vgs - params.Vth; // 1.3V
  bool saturated = (vds >= vgst);
  double expectedIds;

  if (saturated) {
    // Saturation: IDS = 0.5 * Kp * Vgst^2 * (1 + lambda * VDS)
    double vgst_sq = vgst * vgst;
    double modulation = 1.0 + params.lambda * vds;
    expectedIds = 0.5 * params.Kp * vgst_sq * modulation;
  } else {
    // Linear: IDS = Kp * (Vgst * VDS - 0.5 * VDS^2) * (1 + lambda * VDS)
    double modulation = 1.0 + params.lambda * vds;
    expectedIds = params.Kp * (vgst * vds - 0.5 * vds * vds) * modulation;
  }

  // 5. Print diagnostic information
  fmt::print("\n");
  fmt::print("========================================\n");
  fmt::print("MOSFET Level 1 NMOS Verification\n");
  fmt::print("========================================\n");
  fmt::print("\n");
  fmt::print("Parameters:\n");
  fmt::print("  Kp     = {:.3e} A/V^2\n", params.Kp);
  fmt::print("  Vth    = {:.3f} V\n", params.Vth);
  fmt::print("  lambda = {:.3f} 1/V\n", params.lambda);
  fmt::print("\n");
  fmt::print("Operating Point:\n");
  fmt::print("  VGS     = {:.3f} V\n", vgs);
  fmt::print("  VDS     = {:.3f} V\n", vds);
  fmt::print("  VGS-Vth = {:.3f} V\n", vgst);
  fmt::print("  Mode    = {}\n", saturated ? "Saturation" : "Linear");
  fmt::print("\n");
  fmt::print("Results:\n");
  fmt::print("  Our IDS       = {:.6e} A ({:.3f} mA)\n", ids, ids * 1e3);
  fmt::print("  Expected IDS  = {:.6e} A ({:.3f} mA)\n", expectedIds, expectedIds * 1e3);
  fmt::print("  Difference    = {:.6e} A ({:.3f} mA)\n", ids - expectedIds,
             (ids - expectedIds) * 1e3);
  fmt::print("  Relative Err  = {:.6f}%\n", std::abs(ids - expectedIds) / expectedIds * 100.0);
  fmt::print("\n");
  fmt::print("Transconductances:\n");
  double gm = MosfetLevel1::transconductance(vgs, vds, params);
  double gds = MosfetLevel1::outputConductance(vgs, vds, params);
  fmt::print("  gm  = {:.6e} S\n", gm);
  fmt::print("  gds = {:.6e} S\n", gds);
  fmt::print("\n");
  fmt::print("========================================\n");
  fmt::print("\n");

  // 6. Verify match (should be exact since we're comparing against same
  // equations)
  EXPECT_NEAR(ids, expectedIds, expectedIds * 1e-10)
      << "MosfetLevel1 does not match analytical Shichman-Hodges equations";

  // 7. Verify reasonable current magnitude (should be ~0.09 mA in saturation)
  // Calculated: 0.5 * 100e-6 * 1.3^2 * 1.1 = 9.295e-5 A = 0.093 mA
  EXPECT_GT(ids, 50e-6) << "Current too low (< 0.05 mA)";
  EXPECT_LT(ids, 200e-6) << "Current too high (> 0.2 mA)";
}

/* ---------------------- Linear Region Verification ---------------------- */

/**
 * @test
 * Verify MosfetLevel1 in linear region (VDS < VGS - Vth).
 */
TEST(MosfetLevel1, VerifyLinearRegion) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.02};

  // Linear region: VGS = 2.0V, VDS = 0.5V (< VGS - Vth = 1.3V)
  double vgs = 2.0;
  double vds = 0.5;

  double ids = MosfetLevel1::current(vgs, vds, params);
  double vgst = vgs - params.Vth;
  double modulation = 1.0 + params.lambda * vds;
  double expectedIds = params.Kp * (vgst * vds - 0.5 * vds * vds) * modulation;

  fmt::print("\n");
  fmt::print("Linear Region Test\n");
  fmt::print("==================\n");
  fmt::print("VGS = {:.3f} V, VDS = {:.3f} V\n", vgs, vds);
  fmt::print("Mode: Linear (VDS < VGS-Vth)\n");
  fmt::print("IDS = {:.6e} A ({:.3f} mA)\n", ids, ids * 1e3);
  fmt::print("Expected = {:.6e} A ({:.3f} mA)\n\n", expectedIds, expectedIds * 1e3);

  EXPECT_NEAR(ids, expectedIds, expectedIds * 1e-10);
}

/* ----------------------- Cutoff Region Verification --------------------- */

/**
 * @test
 * Verify MosfetLevel1 in cutoff region (VGS < Vth).
 */
TEST(MosfetLevel1, VerifyCutoffRegion) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.02};

  // Cutoff: VGS = 0.5V (< Vth = 0.7V)
  double vgs = 0.5;
  double vds = 5.0;

  double ids = MosfetLevel1::current(vgs, vds, params);

  fmt::print("\n");
  fmt::print("Cutoff Region Test\n");
  fmt::print("==================\n");
  fmt::print("VGS = {:.3f} V, VDS = {:.3f} V\n", vgs, vds);
  fmt::print("Mode: Cutoff (VGS < Vth)\n");
  fmt::print("IDS = {:.6e} A\n\n", ids);

  EXPECT_EQ(ids, 0.0) << "Current should be zero in cutoff";
}

/* --------------------- Channel Modulation Verification ------------------ */

/**
 * @test
 * Verify channel-length modulation (lambda) effect.
 */
TEST(MosfetLevel1, VerifyChannelModulation) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.02};

  // Saturation region with different VDS values
  double vgs = 2.0;

  // Without modulation (lambda = 0)
  MosfetLevel1Params noModParams = params;
  noModParams.lambda = 0.0;

  double vds1 = 2.0;
  double vds2 = 5.0;
  double ids1_noMod = MosfetLevel1::current(vgs, vds1, noModParams);
  double ids2_noMod = MosfetLevel1::current(vgs, vds2, noModParams);
  double ids1_withMod = MosfetLevel1::current(vgs, vds1, params);
  double ids2_withMod = MosfetLevel1::current(vgs, vds2, params);

  fmt::print("\n");
  fmt::print("Channel-Length Modulation Test\n");
  fmt::print("===============================\n");
  fmt::print("VGS = {:.3f} V (saturation mode)\n\n", vgs);
  fmt::print("Without modulation (lambda = 0):\n");
  fmt::print("  VDS = {:.1f} V: IDS = {:.6e} A\n", vds1, ids1_noMod);
  fmt::print("  VDS = {:.1f} V: IDS = {:.6e} A\n", vds2, ids2_noMod);
  fmt::print("  (Should be equal in ideal saturation)\n\n");
  fmt::print("With modulation (lambda = {:.3f}):\n", params.lambda);
  fmt::print("  VDS = {:.1f} V: IDS = {:.6e} A\n", vds1, ids1_withMod);
  fmt::print("  VDS = {:.1f} V: IDS = {:.6e} A\n", vds2, ids2_withMod);
  fmt::print("  Ratio: {:.3f} (higher VDS -> higher IDS)\n\n", ids2_withMod / ids1_withMod);

  // Without modulation, currents should be equal
  EXPECT_NEAR(ids1_noMod, ids2_noMod, ids1_noMod * 1e-10);

  // With modulation, higher VDS should give higher current
  EXPECT_GT(ids2_withMod, ids1_withMod);

  // Verify modulation formula: IDS(VDS2) / IDS(VDS1) = (1 + lambda*VDS2) / (1 +
  // lambda*VDS1)
  double expectedRatio = (1.0 + params.lambda * vds2) / (1.0 + params.lambda * vds1);
  double actualRatio = ids2_withMod / ids1_withMod;
  EXPECT_NEAR(actualRatio, expectedRatio, expectedRatio * 1e-10);
}

/* =================== ngspice Comparison Tests =========================== */

using sim::electronics::algorithms::spice::ngspice::NgspiceStatus;
using sim::electronics::algorithms::spice::ngspice::NgspiceWrapper;

/**
 * @test
 * Compare MosfetLevel1 against ngspice for NMOS in saturation.
 *
 * Loads a SPICE netlist into ngspice, runs DC operating point, extracts
 * the drain current, and compares against our MosfetLevel1::current().
 */
TEST(MosfetLevel1, CompareAgainstNgspiceSaturation) {
  NgspiceWrapper ngspice;

  ASSERT_TRUE(NgspiceWrapper::isLibngspiceAvailable()) << "libngspice not available";

  // SPICE netlist: single NMOS in saturation
  // Note: ngspice NMOS Level 1 uses KP as transconductance (already W/L-normalized
  // in the .model card). Our model's Kp is the raw parameter, so we must match
  // the effective KP = KP_model * W/L.
  // IMPORTANT: First line MUST be the title (no leading blank lines)
  std::string netlist = "Saturation Test\n"
                        "M1 drain gate 0 0 NMOS W=10u L=1u\n"
                        ".model NMOS NMOS (LEVEL=1 VTO=0.7 KP=100u LAMBDA=0.02)\n"
                        "VDS drain 0 DC 5.0\n"
                        "VGS gate  0 DC 2.0\n"
                        ".op\n"
                        ".end\n";

  auto status = ngspice.loadNetlistFromString(netlist);
  ASSERT_EQ(status, NgspiceStatus::OK) << "Failed to load netlist";

  status = ngspice.runDcOperatingPoint();
  ASSERT_EQ(status, NgspiceStatus::OK) << "Failed to run DC operating point";

  // Extract ngspice drain current (from VDS source current)
  // In ngspice, current through voltage source VDS flows into the + terminal
  double ngspiceDrainV = 0.0;
  status = ngspice.getNodeVoltage("drain", ngspiceDrainV);

  fmt::print("\n");
  fmt::print("========================================\n");
  fmt::print("ngspice Comparison: NMOS Saturation\n");
  fmt::print("========================================\n");
  fmt::print("\n");
  fmt::print("ngspice results:\n");

  // Print all available voltages for debugging
  auto& allVoltages = ngspice.getAllNodeVoltages();
  for (auto& [name, voltage] : allVoltages) {
    fmt::print("  {} = {:.6e}\n", name, voltage);
  }
  fmt::print("\n");

  // Our model calculation
  // ngspice KP in .model is the process transconductance parameter.
  // The effective beta = KP * W/L = 100e-6 * 10 = 1e-3 A/V^2
  // Our MosfetLevel1 uses Kp directly (not multiplied by W/L).
  // So to match ngspice: our Kp = ngspice_KP * W/L
  MosfetLevel1Params params;
  params.Kp = 100e-6 * (10e-6 / 1e-6); // KP * W/L = 1e-3
  params.Vth = 0.7;
  params.lambda = 0.02;

  double vgs = 2.0;
  double vds = 5.0;
  double ourIds = MosfetLevel1::current(vgs, vds, params);

  fmt::print("Our MosfetLevel1:\n");
  fmt::print("  Kp (effective) = {:.3e} A/V^2 (KP * W/L)\n", params.Kp);
  fmt::print("  IDS = {:.6e} A ({:.3f} mA)\n", ourIds, ourIds * 1e3);
  fmt::print("\n");

  // Try to find drain current from ngspice
  // ngspice stores branch currents as "@device[current]" or "vds#branch"
  double ngspiceBranchI = 0.0;
  auto branchStatus = ngspice.getNodeVoltage("vds#branch", ngspiceBranchI);

  if (branchStatus == NgspiceStatus::OK) {
    // ngspice convention: current into + terminal is positive
    // For VDS (+ at drain), current flowing into drain is negative IDS
    double ngspiceIds = -ngspiceBranchI;

    fmt::print("ngspice drain current:\n");
    fmt::print("  vds#branch = {:.6e} A\n", ngspiceBranchI);
    fmt::print("  IDS (= -branch) = {:.6e} A ({:.3f} mA)\n", ngspiceIds, ngspiceIds * 1e3);
    fmt::print("\n");

    double diff = ourIds - ngspiceIds;
    double relErr = std::abs(diff) / ngspiceIds * 100.0;
    fmt::print("Comparison:\n");
    fmt::print("  Our IDS     = {:.6e} A\n", ourIds);
    fmt::print("  ngspice IDS = {:.6e} A\n", ngspiceIds);
    fmt::print("  Difference  = {:.6e} A\n", diff);
    fmt::print("  Relative Err = {:.4f}%%\n", relErr);
    fmt::print("\n");
    fmt::print("========================================\n\n");

    // Verify match within 1% tolerance
    EXPECT_NEAR(ourIds, ngspiceIds, ngspiceIds * 0.01)
        << "MosfetLevel1 does not match ngspice within 1%";
  } else {
    fmt::print("Could not find branch current vector.\n");
    fmt::print("Available vectors listed above.\n");
    fmt::print("========================================\n\n");
    GTEST_SKIP() << "Branch current not available in ngspice results";
  }
}

/**
 * @test
 * Compare MosfetLevel1 against ngspice for NMOS in linear region.
 */
TEST(MosfetLevel1, CompareAgainstNgspiceLinear) {
  NgspiceWrapper ngspice;

  ASSERT_TRUE(NgspiceWrapper::isLibngspiceAvailable()) << "libngspice not available";

  std::string netlist = "Linear Test\n"
                        "M1 drain gate 0 0 NMOS W=10u L=1u\n"
                        ".model NMOS NMOS (LEVEL=1 VTO=0.7 KP=100u LAMBDA=0.02)\n"
                        "VDS drain 0 DC 0.5\n"
                        "VGS gate  0 DC 2.0\n"
                        ".op\n"
                        ".end\n";

  auto status = ngspice.loadNetlistFromString(netlist);
  ASSERT_EQ(status, NgspiceStatus::OK);

  status = ngspice.runDcOperatingPoint();
  ASSERT_EQ(status, NgspiceStatus::OK);

  // Our model: Kp = KP * W/L
  MosfetLevel1Params params{.Kp = 100e-6 * (10e-6 / 1e-6), .Vth = 0.7, .lambda = 0.02};
  double ourIds = MosfetLevel1::current(2.0, 0.5, params);

  fmt::print("\n");
  fmt::print("ngspice Comparison: NMOS Linear\n");
  fmt::print("================================\n");

  auto& allVoltages = ngspice.getAllNodeVoltages();
  for (auto& [name, voltage] : allVoltages) {
    fmt::print("  {} = {:.6e}\n", name, voltage);
  }
  fmt::print("\n");

  double ngspiceBranchI = 0.0;
  auto branchStatus = ngspice.getNodeVoltage("vds#branch", ngspiceBranchI);

  if (branchStatus == NgspiceStatus::OK) {
    double ngspiceIds = -ngspiceBranchI;
    double relErr = std::abs(ourIds - ngspiceIds) / ngspiceIds * 100.0;

    fmt::print("Our IDS     = {:.6e} A ({:.3f} mA)\n", ourIds, ourIds * 1e3);
    fmt::print("ngspice IDS = {:.6e} A ({:.3f} mA)\n", ngspiceIds, ngspiceIds * 1e3);
    fmt::print("Relative Err = {:.4f}%%\n\n", relErr);

    EXPECT_NEAR(ourIds, ngspiceIds, ngspiceIds * 0.01)
        << "MosfetLevel1 linear region does not match ngspice within 1%";
  } else {
    GTEST_SKIP() << "Branch current not available";
  }
}
