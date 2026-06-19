/**
 * @file LolaTerrainModel_uTest.cpp
 * @brief Tests for the Moon wrapper around HtileTile.
 */

#include "src/sim/environment/terrain/inc/Htile.hpp"
#include "src/sim/environment/terrain/inc/moon/LolaTerrainModel.hpp"
#include "src/sim/environment/terrain/inc/moon/LunarTerrainConstants.hpp"

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
using sim::environment::terrain::moon::LolaTerrainModel;
namespace lunar = sim::environment::terrain::moon::lunar;

namespace {

void writeTestTile(const std::string& path, const char* body, const char* refSurface,
                   double refRadius) {
  HtileHeader hdr{};
  htileHeaderInit(hdr);
  std::strncpy(hdr.body, body, sizeof(hdr.body) - 1);
  std::strncpy(hdr.ref_surface, refSurface, sizeof(hdr.ref_surface) - 1);
  hdr.ref_radius_m = refRadius;
  hdr.lat_min_deg = -1.0;
  hdr.lat_max_deg = 1.0;
  hdr.lon_min_deg = -1.0;
  hdr.lon_max_deg = 1.0;
  hdr.dim_lat = 4;
  hdr.dim_lon = 4;
  std::vector<std::int16_t> samples(16, 50);
  HtileWriter w;
  ASSERT_TRUE(w.open(path.c_str(), hdr));
  ASSERT_TRUE(w.writeAllSamples(samples.data(), samples.size() * sizeof(std::int16_t)));
}

class LolaTerrainModelTest : public ::testing::Test {
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
    const std::string PATH = "/tmp/apex_lolawrap_uTest_" + std::string(hint) + "_" +
                             std::to_string(::getpid()) + "_" + std::to_string(counter) + ".htile";
    created_.push_back(PATH);
    return PATH;
  }

private:
  std::vector<std::string> created_;
};

/* ----------------------------- Validation ----------------------------- */

TEST_F(LolaTerrainModelTest, MoonFlavoredAccepted) {
  const std::string PATH = tmp("moon_ok");
  writeTestTile(PATH, "moon", lunar::REF_SURFACE_NAME, lunar::R_REF_M);
  LolaTerrainModel m;
  ASSERT_EQ(m.loadMoon(PATH), Status::SUCCESS);
  EXPECT_TRUE(m.isMoonValid());
}

TEST_F(LolaTerrainModelTest, RejectsRadiusOutOfTolerance) {
  const std::string PATH = tmp("mars");
  writeTestTile(PATH, "mars", "sphere", 3.4e6);
  LolaTerrainModel m;
  EXPECT_EQ(m.loadMoon(PATH), Status::ERROR_FILE_FORMAT_INVALID);
}

TEST_F(LolaTerrainModelTest, RejectsWrongRefSurface) {
  const std::string PATH = tmp("egm96_moon");
  writeTestTile(PATH, "moon", "egm96", lunar::R_REF_M); // bogus combo
  LolaTerrainModel m;
  EXPECT_EQ(m.loadMoon(PATH), Status::ERROR_FILE_FORMAT_INVALID);
}

TEST_F(LolaTerrainModelTest, IsMoonValidFalseWhenNotLoaded) {
  LolaTerrainModel m;
  EXPECT_FALSE(m.isMoonValid());
}

TEST_F(LolaTerrainModelTest, MissingFileRejected) {
  LolaTerrainModel m;
  EXPECT_EQ(m.loadMoon("/tmp/apex_lolawrap_does_not_exist.htile"), Status::ERROR_DATA_PATH_INVALID);
}

} // namespace
