/**
 * @file SrtmTerrainModel_uTest.cpp
 * @brief Tests for the Earth wrapper around HtileTile.
 */

#include "src/sim/environment/terrain/inc/Htile.hpp"
#include "src/sim/environment/terrain/inc/earth/SrtmTerrainModel.hpp"
#include "src/sim/environment/terrain/inc/earth/Wgs84TerrainConstants.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using sim::environment::terrain::HtileHeader;
using sim::environment::terrain::htileHeaderInit;
using sim::environment::terrain::HtileWriter;
using sim::environment::terrain::Status;
using sim::environment::terrain::earth::SrtmTerrainModel;
namespace wgs84 = sim::environment::terrain::earth::wgs84;

namespace {

/// Write a 4x4 synthetic htile with caller-controlled body/ref values.
void writeTestTile(const std::string& path, const char* body, const char* refSurface,
                   double refRadius) {
  HtileHeader hdr{};
  htileHeaderInit(hdr);
  std::strncpy(hdr.body, body, sizeof(hdr.body) - 1);
  std::strncpy(hdr.ref_surface, refSurface, sizeof(hdr.ref_surface) - 1);
  hdr.ref_radius_m = refRadius;
  hdr.lat_min_deg = 0.0;
  hdr.lat_max_deg = 1.0;
  hdr.lon_min_deg = 0.0;
  hdr.lon_max_deg = 1.0;
  hdr.dim_lat = 4;
  hdr.dim_lon = 4;
  std::vector<std::int16_t> samples(16, 100);
  HtileWriter w;
  ASSERT_TRUE(w.open(path.c_str(), hdr));
  ASSERT_TRUE(w.writeAllSamples(samples.data(), samples.size() * sizeof(std::int16_t)));
}

class SrtmTerrainModelTest : public ::testing::Test {
protected:
  void TearDown() override {
    for (const auto& p : created_) {
      std::error_code ec;
      std::filesystem::remove(p, ec);
    }
    created_.clear();
  }
  std::string tmp(const char* hint) {
    static int counter = 0;
    ++counter;
    const std::string PATH = "/tmp/apex_srtmwrap_uTest_" + std::string(hint) + "_" +
                             std::to_string(::getpid()) + "_" + std::to_string(counter) + ".htile";
    created_.push_back(PATH);
    return PATH;
  }

private:
  std::vector<std::string> created_;
};

/* ----------------------------- Validation ----------------------------- */

TEST_F(SrtmTerrainModelTest, EarthFlavoredAccepted) {
  const std::string PATH = tmp("earth_ok");
  writeTestTile(PATH, "earth", wgs84::REF_SURFACE_NAME, wgs84::R_EQ_M);
  SrtmTerrainModel m;
  ASSERT_EQ(m.loadEarth(PATH), Status::SUCCESS);
  EXPECT_TRUE(m.isEarthValid());
  EXPECT_TRUE(m.isLoaded());
}

TEST_F(SrtmTerrainModelTest, AcceptsRadiusWithinTolerance) {
  const std::string PATH = tmp("near_wgs84");
  writeTestTile(PATH, "earth", wgs84::REF_SURFACE_NAME, wgs84::R_EQ_M + 0.5 * wgs84::R_TOLERANCE_M);
  SrtmTerrainModel m;
  EXPECT_EQ(m.loadEarth(PATH), Status::SUCCESS);
}

TEST_F(SrtmTerrainModelTest, RejectsRadiusOutOfTolerance) {
  const std::string PATH = tmp("mars");
  writeTestTile(PATH, "mars", "sphere", 3.4e6); // way off Earth
  SrtmTerrainModel m;
  EXPECT_EQ(m.loadEarth(PATH), Status::ERROR_FILE_FORMAT_INVALID);
}

TEST_F(SrtmTerrainModelTest, RejectsWrongRefSurface) {
  const std::string PATH = tmp("sphere_earth");
  writeTestTile(PATH, "earth", "sphere", wgs84::R_EQ_M); // bogus combo
  SrtmTerrainModel m;
  EXPECT_EQ(m.loadEarth(PATH), Status::ERROR_FILE_FORMAT_INVALID);
}

TEST_F(SrtmTerrainModelTest, IsEarthValidFalseWhenNotLoaded) {
  SrtmTerrainModel m;
  EXPECT_FALSE(m.isEarthValid());
}

TEST_F(SrtmTerrainModelTest, MissingFileRejected) {
  SrtmTerrainModel m;
  EXPECT_EQ(m.loadEarth("/tmp/apex_srtmwrap_does_not_exist.htile"),
            Status::ERROR_DATA_PATH_INVALID);
}

/* ----------------------------- Inherited HtileTile API ----------------------------- */

TEST_F(SrtmTerrainModelTest, ElevationApiInherited) {
  const std::string PATH = tmp("api");
  writeTestTile(PATH, "earth", wgs84::REF_SURFACE_NAME, wgs84::R_EQ_M);
  SrtmTerrainModel m;
  ASSERT_EQ(m.loadEarth(PATH), Status::SUCCESS);
  double H = -999.0;
  ASSERT_EQ(m.elevationAt(0.5 * 0.0174533, 0.5 * 0.0174533, H), Status::SUCCESS);
  EXPECT_DOUBLE_EQ(H, 100.0); // synthetic samples were all 100
}

} // namespace
