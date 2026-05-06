/**
 * @file BootstrapCapsCompliance_uTest.cpp
 * @brief Verify bootstrap-cap data file matches Intel/Lajos refs.
 *
 * Anchored to:
 *   - Faggin's documented count: 66 bootstrap loads
 *     (insanity4004 + righto.com cross-references)
 *   - Lajos analyzer layout extraction: 66 C-records tagged "Bootstrap"
 *   - Intel datasheet (intel-4004.pdf 8-21): CDB = 7 pF data-bus pin cap
 *   - Layout extraction maps pixel area to fF via Cox = 6.9e-4 F/m^2
 *     and 1.5 um/pixel (verified against 12 mm^2 die size)
 */

#include "src/sim/electronics/intel4004/grid/inc/Intel4004GridLevel2.hpp"
#include "src/sim/electronics/intel4004/netlist/inc/SpiceNetlistParser.hpp"

#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>

using sim::electronics::intel4004::Intel4004GridLevel2;
using sim::electronics::intel4004::loadSpiceNetlist;

#ifdef INTEL4004_DATA_DIR
static const std::string SPICE_PATH = INTEL4004_DATA_DIR "/lajos-4004.spice";
static const std::string CAPS_PATH =
    std::string(INTEL4004_DATA_DIR) + "/lajos-4004-bootstrap-caps.txt";
#endif

#ifdef INTEL4004_DATA_DIR

/**
 * @test Bootstrap caps data file is well-formed and produces expected counts.
 *
 * The file format is `<gate> <source> <pixels> <C_femtofarads>` per line.
 * 66 layout-extracted caps + 4 D-bus pin caps (datasheet CDB=7 pF) =
 * 70 total. All entries should resolve to valid net IDs.
 */
TEST(BootstrapCaps, ExpectedCount) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004GridLevel2 grid;
  auto circuit = grid.buildCircuit(NETLIST);

  const std::size_t loaded = grid.loadBootstrapCaps(CAPS_PATH);

  // 66 layout-extracted caps (Faggin's documented count) + 4 D-bus pin
  // caps (datasheet CDB=7 pF on D0..D3) = 70 expected.
  EXPECT_EQ(loaded, 70u)
      << "Expected 66 layout caps + 4 D-bus pin caps = 70 total.\n"
      << "Got " << loaded << ". Either file is missing entries or"
      << " findNet failed to resolve some net names.";
}

/**
 * @test Bootstrap caps file structure: 70 valid entries, no malformed lines.
 */
TEST(BootstrapCaps, FileFormat) {
  std::ifstream f(CAPS_PATH);
  ASSERT_TRUE(f.is_open()) << "Cannot open " << CAPS_PATH;

  std::size_t valid = 0;
  std::size_t comments = 0;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') {
      ++comments;
      continue;
    }
    std::istringstream iss(line);
    std::string a, b;
    double pixels = 0.0, valueFF = 0.0;
    if (iss >> a >> b >> pixels >> valueFF) {
      EXPECT_GT(valueFF, 0.0)
          << "Cap value must be positive: '" << line << "'";
      ++valid;
    }
  }

  EXPECT_EQ(valid, 70u) << "Expected 70 valid cap entries in " << CAPS_PATH;
  EXPECT_GT(comments, 0u) << "Header comments should be present";
}

/**
 * @test Layout-extracted cap values are within physically plausible range.
 *
 * Per-cap values are extracted from Lajos's poly+diffusion BMPs using
 * Cox = 6.9e-4 F/m^2 (50 nm SiO2) and 1.5 um/pixel. Resulting range:
 * 5 fF to 1108 fF (median 405 fF). Values outside this range suggest
 * extraction or scaling errors.
 *
 * The 4 pin caps from datasheet are 7000 fF each (CDB=7 pF).
 */
TEST(BootstrapCaps, ValueRange) {
  std::ifstream f(CAPS_PATH);
  ASSERT_TRUE(f.is_open());

  double minLayout = 1e9, maxLayout = 0;
  std::size_t pinCaps = 0;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream iss(line);
    std::string a, b;
    double pixels = 0.0, valueFF = 0.0;
    if (!(iss >> a >> b >> pixels >> valueFF)) continue;

    if (b == "GND" && valueFF > 5000.0) {
      // Datasheet pin cap (7 pF = 7000 fF)
      ++pinCaps;
      EXPECT_NEAR(valueFF, 7000.0, 100.0)
          << "D-bus pin cap should match CDB=7 pF: line '" << line << "'";
    } else {
      // Layout-extracted bootstrap cap
      minLayout = std::min(minLayout, valueFF);
      maxLayout = std::max(maxLayout, valueFF);
    }
  }

  EXPECT_EQ(pinCaps, 4u) << "Expected 4 D-bus pin caps (D0/D1/D2/D3)";

  // Layout extraction observes 5 fF to 1108 fF.
  EXPECT_GE(minLayout, 1.0)
      << "Smallest layout cap < 1 fF suggests extraction error";
  EXPECT_LE(maxLayout, 2000.0)
      << "Largest layout cap > 2 pF suggests flood-fill leak (extraction error)";
}

/**
 * @test The 66 layout-extracted bootstrap pairs include the OPA pass paths.
 *
 * Each OPA bit has an indirect D-bus path through bootstrap-load gates
 * with explicit caps. Verify key signal-name caps are present (e.g.,
 * OPA-IB which gates the OPA latch capture).
 */
TEST(BootstrapCaps, OpaIbCapPresent) {
  std::ifstream f(CAPS_PATH);
  ASSERT_TRUE(f.is_open());

  bool foundOpaIb = false;
  bool foundAccAdac = false;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    if (line.find(" OPA-IB ") != std::string::npos) foundOpaIb = true;
    if (line.find(" ACC-ADAC ") != std::string::npos) foundAccAdac = true;
  }

  EXPECT_TRUE(foundOpaIb) << "OPA-IB cap (OPA latch capture gate) missing";
  EXPECT_TRUE(foundAccAdac) << "ACC-ADAC cap (ALU output bus gate) missing";
}

#endif  // INTEL4004_DATA_DIR
