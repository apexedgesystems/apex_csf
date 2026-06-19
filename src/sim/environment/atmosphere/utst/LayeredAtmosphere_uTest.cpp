/**
 * @file LayeredAtmosphere_uTest.cpp
 * @brief Tests for the hydrostatic layered atmosphere model.
 *
 * Validation against USSA76 published reference values (NASA TM-X-74335).
 * The reference values used here are widely reproduced in aerospace texts
 * and any USSA76 calculator. Tolerances are <1% on T, <1% on P at standard
 * altitudes within the table -- the implementation is deterministic and
 * the inputs (layer table) are exact.
 */

#include "src/sim/environment/atmosphere/inc/Atm.hpp"
#include "src/sim/environment/atmosphere/inc/LayeredAtmosphere.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using sim::environment::atmosphere::AtmHeader;
using sim::environment::atmosphere::atmHeaderInit;
using sim::environment::atmosphere::atmMakeLayer;
using sim::environment::atmosphere::AtmModelType;
using sim::environment::atmosphere::AtmosphereState;
using sim::environment::atmosphere::AtmRecord;
using sim::environment::atmosphere::AtmWriter;
using sim::environment::atmosphere::isSuccess;
using sim::environment::atmosphere::LayeredAtmosphere;
using sim::environment::atmosphere::Status;

namespace {

/* ----------------------------- Helpers ----------------------------- */

/// USSA76 layer table (NASA TM-X-74335). 7 layers from sea level to 86 km.
std::vector<LayeredAtmosphere::Layer> ussa76Layers() {
  return {
      {0.0, 288.15, 101325.0, -0.0065},     // troposphere
      {11000.0, 216.65, 22632.06, 0.0},     // tropopause
      {20000.0, 216.65, 5474.889, 0.001},   // strat 1
      {32000.0, 228.65, 868.0187, 0.0028},  // strat 2
      {47000.0, 270.65, 110.9063, 0.0},     // stratopause
      {51000.0, 270.65, 66.93887, -0.0028}, // mesosphere 1
      {71000.0, 214.65, 3.95642, -0.002},   // mesosphere 2
  };
}

/// File-test fixture for the file-loading tests.
class LayeredAtmFileTest : public ::testing::Test {
protected:
  void TearDown() override {
    for (const auto& p : created_) {
      std::error_code ec;
      std::filesystem::remove(p, ec);
    }
    created_.clear();
  }
  std::string tmpPath(const char* hint) {
    static int counter = 0;
    ++counter;
    return makeAndTrack("/tmp/layered_atm_uTest_" + std::string(hint) + "_" +
                        std::to_string(::getpid()) + "_" + std::to_string(counter) + ".atm");
  }

  /// Write a USSA76 .atm file at `path` and return path.
  std::string writeUssa76File(const std::string& path) {
    AtmHeader hdr{};
    atmHeaderInit(hdr);
    std::strncpy(hdr.body, "earth", sizeof(hdr.body) - 1);
    hdr.model_type = static_cast<std::uint8_t>(AtmModelType::kLayered);
    const auto LS = ussa76Layers();
    hdr.n_records = static_cast<std::uint16_t>(LS.size());
    std::vector<AtmRecord> recs;
    recs.reserve(LS.size());
    for (const auto& l : LS) {
      recs.push_back(atmMakeLayer(l.base_alt_m, l.base_T_K, l.base_P_Pa, l.lapse_K_per_m));
    }
    AtmWriter w;
    if (!w.open(path.c_str(), hdr) || !w.writeAllRecords(recs.data(), recs.size())) {
      return "";
    }
    return path;
  }

private:
  std::string makeAndTrack(const std::string& p) {
    created_.push_back(p);
    return p;
  }
  std::vector<std::string> created_;
};

/* ----------------------------- Default Construction ----------------------------- */

TEST(LayeredAtmosphere, DefaultIsNotLoaded) {
  LayeredAtmosphere a;
  EXPECT_FALSE(a.isLoaded());
  EXPECT_EQ(a.numLayers(), 0u);
  EXPECT_FALSE(a.isVacuum());
  AtmosphereState s;
  EXPECT_EQ(a.query(0.0, 0.0, 0.0, s), Status::ERROR_NOT_INITIALIZED);
}

/* ----------------------------- initFromMemory ----------------------------- */

TEST(LayeredAtmosphere, InitFromMemoryAcceptsUssa76) {
  LayeredAtmosphere a;
  EXPECT_TRUE(isSuccess(a.initFromMemory(ussa76Layers(), 287.058, 1.4, 9.80665)));
  EXPECT_TRUE(a.isLoaded());
  EXPECT_EQ(a.numLayers(), 7u);
  EXPECT_DOUBLE_EQ(a.gasConstant(), 287.058);
  EXPECT_DOUBLE_EQ(a.gamma(), 1.4);
  EXPECT_DOUBLE_EQ(a.surfaceGravity(), 9.80665);
}

TEST(LayeredAtmosphere, InitFromMemoryRejectsEmpty) {
  LayeredAtmosphere a;
  EXPECT_EQ(a.initFromMemory({}, 287.058, 1.4, 9.80665), Status::ERROR_PARAM_LAYERS_EMPTY);
}

TEST(LayeredAtmosphere, InitFromMemoryRejectsNonMonotonic) {
  std::vector<LayeredAtmosphere::Layer> bad = ussa76Layers();
  bad[3].base_alt_m = bad[2].base_alt_m - 1.0; // out of order
  LayeredAtmosphere a;
  EXPECT_EQ(a.initFromMemory(bad, 287.058, 1.4, 9.80665), Status::ERROR_PARAM_LAYERS_NONMONOTONIC);
}

TEST(LayeredAtmosphere, InitFromMemoryRejectsBadThermo) {
  LayeredAtmosphere a;
  // R <= 0
  EXPECT_EQ(a.initFromMemory(ussa76Layers(), 0.0, 1.4, 9.80665),
            Status::ERROR_PARAM_GAS_CONST_INVALID);
  // gamma <= 1
  EXPECT_EQ(a.initFromMemory(ussa76Layers(), 287.058, 1.0, 9.80665),
            Status::ERROR_PARAM_GAS_CONST_INVALID);
  // g0 <= 0
  EXPECT_EQ(a.initFromMemory(ussa76Layers(), 287.058, 1.4, 0.0),
            Status::ERROR_PARAM_GAS_CONST_INVALID);
}

TEST(LayeredAtmosphere, InitFromMemoryRejectsNonPositiveTemp) {
  std::vector<LayeredAtmosphere::Layer> bad = ussa76Layers();
  bad[2].base_T_K = 0.0;
  LayeredAtmosphere a;
  EXPECT_EQ(a.initFromMemory(bad, 287.058, 1.4, 9.80665), Status::ERROR_PARAM_TEMP_INVALID);
}

TEST(LayeredAtmosphere, InitFromMemoryRejectsNegativePressure) {
  std::vector<LayeredAtmosphere::Layer> bad = ussa76Layers();
  bad[1].base_P_Pa = -1.0;
  LayeredAtmosphere a;
  EXPECT_EQ(a.initFromMemory(bad, 287.058, 1.4, 9.80665), Status::ERROR_PARAM_PRESSURE_INVALID);
}

/* ----------------------------- USSA76 Reference Validation ----------------------------- */

// Reference values: NASA TM-X-74335 (1976) standard altitudes table.
// Reproduced widely; these are the canonical published numbers.

TEST(LayeredAtmosphereUssa76, SeaLevelMatchesReference) {
  LayeredAtmosphere a;
  ASSERT_TRUE(isSuccess(a.initFromMemory(ussa76Layers(), 287.058, 1.4, 9.80665)));
  AtmosphereState s;
  ASSERT_TRUE(isSuccess(a.query(0.0, 0.0, 0.0, s)));
  // T = 288.15 K, P = 101325 Pa, rho ~ 1.225 kg/m^3.
  EXPECT_NEAR(s.T, 288.15, 0.01);
  EXPECT_NEAR(s.P, 101325.0, 1.0);
  EXPECT_NEAR(s.rho, 1.225, 0.001);
  // Speed of sound ~ 340.294 m/s.
  EXPECT_NEAR(s.a, 340.294, 0.01);
}

TEST(LayeredAtmosphereUssa76, FivekmMatchesReference) {
  LayeredAtmosphere a;
  ASSERT_TRUE(isSuccess(a.initFromMemory(ussa76Layers(), 287.058, 1.4, 9.80665)));
  AtmosphereState s;
  ASSERT_TRUE(isSuccess(a.query(5000.0, 0.0, 0.0, s)));
  // USSA76 reference @ 5000 m: T=255.676, P=54019.9, rho=0.7361.
  EXPECT_NEAR(s.T, 255.676, 0.5);
  EXPECT_NEAR(s.P, 54019.9, 100.0);  // ~0.2% tolerance
  EXPECT_NEAR(s.rho, 0.7361, 0.005); // ~0.7% tolerance
}

TEST(LayeredAtmosphereUssa76, ElevenkmTropopauseMatchesReference) {
  LayeredAtmosphere a;
  ASSERT_TRUE(isSuccess(a.initFromMemory(ussa76Layers(), 287.058, 1.4, 9.80665)));
  AtmosphereState s;
  ASSERT_TRUE(isSuccess(a.query(11000.0, 0.0, 0.0, s)));
  // Right at the tropopause base. T=216.65, P=22632.06.
  EXPECT_NEAR(s.T, 216.65, 0.01);
  EXPECT_NEAR(s.P, 22632.06, 1.0);
}

TEST(LayeredAtmosphereUssa76, TwentykmStratosphereMatchesReference) {
  LayeredAtmosphere a;
  ASSERT_TRUE(isSuccess(a.initFromMemory(ussa76Layers(), 287.058, 1.4, 9.80665)));
  AtmosphereState s;
  ASSERT_TRUE(isSuccess(a.query(20000.0, 0.0, 0.0, s)));
  // T=216.65, P=5474.889.
  EXPECT_NEAR(s.T, 216.65, 0.01);
  EXPECT_NEAR(s.P, 5474.889, 1.0);
}

TEST(LayeredAtmosphereUssa76, ThirtyKmStratosphereSegment) {
  // Note: USSA76 reference tables are typically published against
  // GEOMETRIC altitude (Z = 30 km gives P = 1197.03 Pa). Our model uses
  // the altitude argument directly as GEOPOTENTIAL altitude (H), which
  // is the variable the USSA76 hydrostatic formula expects. At 30 km
  // the difference is ~140 m of equivalent layer position, which
  // shifts P by ~2%. We assert the geopotential prediction here.
  LayeredAtmosphere a;
  ASSERT_TRUE(isSuccess(a.initFromMemory(ussa76Layers(), 287.058, 1.4, 9.80665)));
  AtmosphereState s;
  ASSERT_TRUE(isSuccess(a.query(30000.0, 0.0, 0.0, s)));
  // T(H=30000) = 216.65 + 0.001 * (30000 - 20000) = 226.65 K
  EXPECT_NEAR(s.T, 226.65, 0.01);
  // P(H=30000) = 5474.889 * (216.65/226.65)^34.163 ~ 1171 Pa.
  EXPECT_NEAR(s.P, 1171.0, 2.0);
}

TEST(LayeredAtmosphereUssa76, FiftykmStratopauseMatchesReference) {
  LayeredAtmosphere a;
  ASSERT_TRUE(isSuccess(a.initFromMemory(ussa76Layers(), 287.058, 1.4, 9.80665)));
  AtmosphereState s;
  ASSERT_TRUE(isSuccess(a.query(50000.0, 0.0, 0.0, s)));
  // 50 km is in the isothermal stratopause layer (47-51 km, T=270.65).
  // Reference P @ 50 km: ~75.9448 Pa.
  EXPECT_NEAR(s.T, 270.65, 0.01);
  EXPECT_NEAR(s.P, 75.9448, 0.5);
}

TEST(LayeredAtmosphereUssa76, SeventyOneKmMesopauseStartMatchesReference) {
  LayeredAtmosphere a;
  ASSERT_TRUE(isSuccess(a.initFromMemory(ussa76Layers(), 287.058, 1.4, 9.80665)));
  AtmosphereState s;
  ASSERT_TRUE(isSuccess(a.query(71000.0, 0.0, 0.0, s)));
  EXPECT_NEAR(s.T, 214.65, 0.01);
  EXPECT_NEAR(s.P, 3.95642, 0.05);
}

TEST(LayeredAtmosphereUssa76, DensityFromIdealGasConsistency) {
  // Verify rho = P / (R*T) holds at every standard altitude.
  LayeredAtmosphere a;
  ASSERT_TRUE(isSuccess(a.initFromMemory(ussa76Layers(), 287.058, 1.4, 9.80665)));
  for (double H : {0.0, 5000.0, 11000.0, 20000.0, 32000.0, 47000.0, 71000.0}) {
    AtmosphereState s;
    ASSERT_TRUE(isSuccess(a.query(H, 0.0, 0.0, s)));
    const double EXPECTED_RHO = s.P / (287.058 * s.T);
    EXPECT_NEAR(s.rho, EXPECTED_RHO, 1e-9) << "at H=" << H;
  }
}

TEST(LayeredAtmosphereUssa76, SoundSpeedFromTemperature) {
  // a = sqrt(gamma * R * T).
  LayeredAtmosphere a;
  ASSERT_TRUE(isSuccess(a.initFromMemory(ussa76Layers(), 287.058, 1.4, 9.80665)));
  AtmosphereState s;
  ASSERT_TRUE(isSuccess(a.query(0.0, 0.0, 0.0, s)));
  EXPECT_NEAR(s.a, std::sqrt(1.4 * 287.058 * 288.15), 1e-6);
}

/* ----------------------------- Boundary Behavior ----------------------------- */

TEST(LayeredAtmosphere, BelowFirstLayerClampsToFirst) {
  LayeredAtmosphere a;
  ASSERT_TRUE(isSuccess(a.initFromMemory(ussa76Layers(), 287.058, 1.4, 9.80665)));
  AtmosphereState s_neg, s_zero;
  ASSERT_TRUE(isSuccess(a.query(-100.0, 0.0, 0.0, s_neg)));
  ASSERT_TRUE(isSuccess(a.query(0.0, 0.0, 0.0, s_zero)));
  // -100 m extrapolates the troposphere lapse rate: T=288.15+(-0.0065)*-100 = 288.8.
  EXPECT_GT(s_neg.T, s_zero.T); // colder lapse means warmer below sea level
}

TEST(LayeredAtmosphere, FarAboveTopWarnsOutOfRange) {
  LayeredAtmosphere a;
  ASSERT_TRUE(isSuccess(a.initFromMemory(ussa76Layers(), 287.058, 1.4, 9.80665)));
  AtmosphereState s{};
  // 200 km is way past the table + extrapolation band. The state is left
  // unmodified (default-zero) on the out-of-range warning.
  EXPECT_EQ(a.query(200000.0, 0.0, 0.0, s), Status::WARN_OUT_OF_VALID_RANGE);
  EXPECT_DOUBLE_EQ(s.rho, 0.0);
}

TEST(LayeredAtmosphere, NanAltitudeRejected) {
  LayeredAtmosphere a;
  ASSERT_TRUE(isSuccess(a.initFromMemory(ussa76Layers(), 287.058, 1.4, 9.80665)));
  AtmosphereState s{};
  EXPECT_EQ(a.query(std::nan(""), 0.0, 0.0, s), Status::ERROR_PARAM_ALT_INVALID);
}

TEST(LayeredAtmosphere, JustAboveTopWithinExtrapolationOk) {
  // Top layer base = 71 km. Extrapolation tolerance = +5 km. 73 km should
  // still answer.
  LayeredAtmosphere a;
  ASSERT_TRUE(isSuccess(a.initFromMemory(ussa76Layers(), 287.058, 1.4, 9.80665)));
  AtmosphereState s;
  EXPECT_TRUE(isSuccess(a.query(73000.0, 0.0, 0.0, s)));
  EXPECT_GT(s.T, 200.0); // sane temperature
}

TEST(LayeredAtmosphere, AltitudeBoundsReflectTable) {
  LayeredAtmosphere a;
  ASSERT_TRUE(isSuccess(a.initFromMemory(ussa76Layers(), 287.058, 1.4, 9.80665)));
  EXPECT_DOUBLE_EQ(a.minAltitudeM(), 0.0);     // first layer base
  EXPECT_DOUBLE_EQ(a.maxAltitudeM(), 76000.0); // 71000 + 5000 tolerance
}

TEST(LayeredAtmosphere, NotVacuum) {
  LayeredAtmosphere a;
  ASSERT_TRUE(isSuccess(a.initFromMemory(ussa76Layers(), 287.058, 1.4, 9.80665)));
  EXPECT_FALSE(a.isVacuum());
}

TEST(LayeredAtmosphere, CloseResetsState) {
  LayeredAtmosphere a;
  ASSERT_TRUE(isSuccess(a.initFromMemory(ussa76Layers(), 287.058, 1.4, 9.80665)));
  ASSERT_TRUE(a.isLoaded());
  a.close();
  EXPECT_FALSE(a.isLoaded());
  EXPECT_EQ(a.numLayers(), 0u);
}

/* ----------------------------- File Load Round-Trip ----------------------------- */

TEST_F(LayeredAtmFileTest, LoadFromAtmFileMatchesMemoryInit) {
  const std::string PATH = writeUssa76File(tmpPath("ussa76"));
  ASSERT_FALSE(PATH.empty());

  LayeredAtmosphere fileBacked;
  ASSERT_TRUE(isSuccess(fileBacked.load(PATH)));
  ASSERT_EQ(fileBacked.numLayers(), 7u);

  LayeredAtmosphere memBacked;
  ASSERT_TRUE(isSuccess(memBacked.initFromMemory(ussa76Layers(), 287.058, 1.4, 9.80665)));

  // Spot-check at a handful of altitudes -- both must agree exactly.
  for (double H : {0.0, 5000.0, 11000.0, 20000.0, 47000.0, 71000.0}) {
    AtmosphereState sf, sm;
    ASSERT_TRUE(isSuccess(fileBacked.query(H, 0.0, 0.0, sf)));
    ASSERT_TRUE(isSuccess(memBacked.query(H, 0.0, 0.0, sm)));
    EXPECT_DOUBLE_EQ(sf.T, sm.T) << "at " << H;
    EXPECT_DOUBLE_EQ(sf.P, sm.P) << "at " << H;
    EXPECT_DOUBLE_EQ(sf.rho, sm.rho) << "at " << H;
    EXPECT_DOUBLE_EQ(sf.a, sm.a) << "at " << H;
  }
}

TEST_F(LayeredAtmFileTest, LoadFromMissingFileFails) {
  LayeredAtmosphere a;
  EXPECT_EQ(a.load("/tmp/horizon_layered_atm_does_not_exist_xyzzy.atm"),
            Status::ERROR_DATA_PATH_INVALID);
  EXPECT_FALSE(a.isLoaded());
}

TEST_F(LayeredAtmFileTest, LoadFromWrongModelTypeFails) {
  // Write a CONSTANT-type .atm file then try to load as layered.
  const std::string PATH = tmpPath("wrong_model");
  AtmHeader hdr{};
  atmHeaderInit(hdr); // defaults to kConstant, n_records=1
  AtmWriter w;
  ASSERT_TRUE(w.open(PATH.c_str(), hdr));
  AtmRecord r{1.225, 288.15, 101325.0, 0.0};
  ASSERT_TRUE(w.writeAllRecords(&r, 1));
  w.close();

  LayeredAtmosphere a;
  EXPECT_EQ(a.load(PATH), Status::ERROR_MODEL_TYPE_MISMATCH);
}

} // namespace
